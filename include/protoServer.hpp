//
// protoServer.hpp
//
#ifndef __PROTO_SERVER_HPP__
#define __PROTO_SERVER_HPP__

#include "epollServer.hpp"
#include "protoCommon.hpp"
#include <google/protobuf/message.h>

namespace gen {

//
// Send and receive Protobuf messages
//
class ProtoServer : public EpollServer
{
public:
    ProtoServer(int threadPoolSize) : EpollServer(threadPoolSize) {}
    virtual ~ProtoServer() = default;

protected:
    // Override EpollServer::OnInit() to be pure virtual (= 0) to force
    // derived classes to provide a concrete implementation.
    virtual bool OnInit() override = 0;

    struct Context
    {
        Context(const std::map<std::string, std::string>& _metadata) : metadata(_metadata) {}
        ~Context() = default;
        void SetError(const std::string& err) const { errMsg = err; }
        const std::string& GetError() const { return errMsg; }

        std::string GetMetadata(const char* key) const
        {
            if(auto itr = metadata.find(key); itr != metadata.end())
                return std::string(itr->second.data(), itr->second.size());
            else
                return "";
        }

    private:
        const std::map<std::string, std::string>& metadata;
        mutable std::string errMsg;
    };

    // Note: Only derived classes can bind their handler (class member functions)
    template<class SERVER, class REQ, class RESP>
    bool Bind(void (SERVER::*fptr)(const Context& ctx, const REQ&, RESP&))
    {
        // Check if we already have handler for this request type
        std::string reqName = REQ().GetTypeName();
        if(auto itr = mHandlerMap.find(reqName); itr != mHandlerMap.end())
        {
            OnError(__FNAME__, __LINE__, "Failed to bind request " + reqName + ": it's already bound");
            return false;
        }
        auto handler = new (std::nothrow) HandlerImpl<SERVER, REQ, RESP>((SERVER*)this, fptr);
        mHandlerMap[reqName].reset(handler);
        return true;
    }

private:
    // EpollServer overrides
    virtual std::shared_ptr<ClientContext> MakeClientContext() override final;
    virtual bool OnRead(std::shared_ptr<ClientContext>& client) override final;
    virtual bool OnWrite(std::shared_ptr<ClientContext>& client) override final;

    // Base class for service-specific HandlerImpl class
    struct Handler
    {
        Handler() = default;
        virtual ~Handler() = default;
        virtual bool Call(const Context& ctx,
                          const std::string& reqData, std::string& respData) = 0;
    };

    template<class SERVER, class REQ, class RESP>
    struct HandlerImpl : public Handler
    {
        typedef void (SERVER::*HANDLER_FPTR)(const Context& ctx, const REQ&, RESP&);
        HandlerImpl(SERVER* _srv, HANDLER_FPTR _fptr) : srv(_srv), fptr(_fptr) {}
        virtual bool Call(const Context& ctx,
                          const std::string& reqData, std::string& respData) override;
        SERVER* srv = nullptr;
        HANDLER_FPTR fptr = nullptr;
    };

    Handler* GetHandler(const std::string& reqName, std::string& errMsg);

    struct ClientContextImpl : public ClientContext
    {
        // Message Processing State
        enum class MessageState
        {
            READING_REQ_NAME = 100,
            READING_REQ,
            SENDING_ACK,
            SENDING_NACK,
            SENDING_RESP
        };

        MessageState messageState{MessageState::READING_REQ_NAME};
        Handler* handler{nullptr};
        std::string respData;
        std::string errMsg;

        // Helper function to reset message unit
        void Reset()
        {
            messageState = MessageState::READING_REQ_NAME;
            respData.clear();
            errMsg.clear();
            handler = nullptr;
        }
    };

private:
    std::map<const std::string, std::unique_ptr<Handler>> mHandlerMap;
};

inline std::shared_ptr<EpollServer::ClientContext> ProtoServer::MakeClientContext()
{
    return std::make_shared<ClientContextImpl>();
}

inline bool ProtoServer::OnRead(std::shared_ptr<EpollServer::ClientContext>& client_)
{
    ClientContextImpl* client = dynamic_cast<ClientContextImpl*>(client_.get());
    int clientFd = client->fd;
    std::string errMsg;

    if(client->messageState == ClientContextImpl::MessageState::READING_REQ_NAME)
    {
        std::string reqName;
        if(!ProtoRecvData(clientFd, PROTO_CODE::REQ_NAME, reqName, 0, errMsg))
        {
            if(errno == ENOTCONN)
            {
                // Note: This is the case when EpollServer calls OnRead() after seeing
                // an EPOLLIN event when the client closes the socket gracefully using close().
                // See EpollServer comments for more details. This is not an error.
                if(mVerbose)
                    OnInfo(__FNAME__, __LINE__, "Socket is not connected");
            }
            else
            {
                OnError(__FNAME__, __LINE__, std::string("Failed to receive REQ_NAME code: ") + errMsg);
            }
            return false;
        }

        // Do we have a handler to call for this request?
        client->handler = GetHandler(reqName, errMsg);
        if(client->handler)
        {
            client->messageState = ClientContextImpl::MessageState::SENDING_ACK;
        }
        else
        {
            client->errMsg = std::move(errMsg);
            client->messageState = ClientContextImpl::MessageState::SENDING_NACK;
        }
        return true;
    }
    else if(client->messageState == ClientContextImpl::MessageState::READING_REQ)
    {
        // Receive REQ (request data)
        std::string reqData;
        if(!ProtoRecvData(clientFd, PROTO_CODE::REQ, reqData, 0, errMsg))
        {
            OnError(__FNAME__, __LINE__, std::string("Failed to receive REQ (request data): ") + errMsg);
            return false;
        }

        // Receive metadata
        std::map<std::string, std::string> metadata;
        if(!ProtoRecvData(clientFd, PROTO_CODE::METADATA, metadata, 0, errMsg))
        {
            OnError(__FNAME__, __LINE__, std::string("Failed to receive METADATA: ") + errMsg);
            return false;
        }

        // Process the request
        Context ctx(metadata);
        client->handler->Call(ctx, reqData, client->respData);
        client->errMsg = std::move(ctx.GetError());

        client->messageState = ClientContextImpl::MessageState::SENDING_RESP;
        return true;
    }
    else
    {
        OnError(__FNAME__, __LINE__, "Unexpected READING state");
        return false;
    }
}

inline bool ProtoServer::OnWrite(std::shared_ptr<EpollServer::ClientContext>& client_)
{
    ClientContextImpl* client = dynamic_cast<ClientContextImpl*>(client_.get());
    int clientFd = client->fd;
    std::string errMsg;

    if(client->messageState == ClientContextImpl::MessageState::SENDING_ACK)
    {
        // Send ACK back to client to indicate we are ready to read more data
        if(!ProtoSendCode(clientFd, PROTO_CODE::ACK, 0, errMsg))
        {
            OnError(__FNAME__, __LINE__, std::string("Failed to send ACK code: ") + errMsg);
            return false;
        }
        client->messageState = ClientContextImpl::MessageState::READING_REQ;
        return true;
    }
    else if(client->messageState == ClientContextImpl::MessageState::SENDING_NACK)
    {
        // Send NACK back to client to indicate we have no handler for the request
        if(!ProtoSendCode(clientFd, PROTO_CODE::NACK, 0, errMsg))
        {
            OnError(__FNAME__, __LINE__, std::string("Failed to send NACK code: ") + errMsg);
            return false;
        }

        // Sent ERR (error message, could be empty)
        if(!ProtoSendData(clientFd, PROTO_CODE::ERR, client->errMsg, 0, errMsg))
        {
            OnError(__FNAME__, __LINE__, std::string("Failed to send ERR (error message): ") + errMsg);
            return false;
        }
        client->Reset();    // Reset for a next message
        return true;
    }
    else if(client->messageState == ClientContextImpl::MessageState::SENDING_RESP)
    {
        // Send the response data
        if(!ProtoSendData(clientFd, PROTO_CODE::RESP, client->respData, 0, errMsg))
        {
            OnError(__FNAME__, __LINE__, std::string("Failed to send RESP (response data): ") + errMsg);
            return false;
        }

        // Sent ERR (error message, could be empty)
        if(!ProtoSendData(clientFd, PROTO_CODE::ERR, client->errMsg, 0, errMsg))
        {
            OnError(__FNAME__, __LINE__, std::string("Failed to send ERR (return value): ") + errMsg);
            return false;
        }
        client->Reset();    // Reset for a next message
        return true;
    }
    else
    {
        OnError(__FNAME__, __LINE__, "Unexpected SENDING state");
        return false;
    }
}

inline ProtoServer::Handler* ProtoServer::GetHandler(const std::string& reqName, std::string& errMsg)
{
    // Do we have a handler to call for this request?
    auto itr = mHandlerMap.find(reqName);
    if(itr == mHandlerMap.end())
    {
        errMsg = "Unknown request: '" + reqName + "'";
        return nullptr;
    }

    auto& handler = itr->second;
    if(!handler)
    {
        errMsg = "Invalid (null) request handler: '" + reqName + "'";
        return nullptr;
    }

    return handler.get();
}

template<class SERVER, class REQ, class RESP>
bool ProtoServer::HandlerImpl<SERVER, REQ, RESP>::Call(const ProtoServer::Context& ctx,
                                                       const std::string& reqData, std::string& respData)
{
    REQ req;
    if(!req.ParseFromString(reqData))
    {
        ctx.SetError("Failed to read protobuf request message");
        return false;
    }

    // Call the handler function
    RESP resp;
    (srv->*fptr)(ctx, req, resp);

    // Serialize response protobuf message to string
    if(!resp.SerializeToString(&respData))
    {
        ctx.SetError("Failed to write protobuf request message");
        return false;
    }

    return true;
}

} // namespace gen

#endif // __PROTO_SERVER_HPP__
