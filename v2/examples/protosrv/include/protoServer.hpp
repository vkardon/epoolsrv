#ifndef __PROTO_SERVER_HPP__
#define __PROTO_SERVER_HPP__

#include "epollServer.hpp"
#include "protoCommon.hpp"
#include <google/protobuf/message.h>
#include <cstring>
#include <sstream>
#include <string.h>

namespace gen {

class ProtoServer : public EpollServer
{
public:
    ProtoServer(int threadPoolSize) : EpollServer(threadPoolSize) {}
    ~ProtoServer() override = default;

protected:
    struct Context
    {
        Context(const std::map<std::string, std::string>& _metadata) : metadata(_metadata) {}
        void SetError(const std::string& err) const { errMsg = err; }
        const std::string& GetError() const { return errMsg; }

        std::string GetMetadata(const char* key) const
        {
            if(auto itr = metadata.find(key); itr != metadata.end()) 
                return std::string(itr->second.data(), itr->second.size());
            return "";
        }

    private:
        const std::map<std::string, std::string>& metadata;
        mutable std::string errMsg;
    };

    template<class SERVER, class REQ, class RESP>
    bool Bind(void (SERVER::*fptr)(const Context& ctx, const REQ&, RESP&))
    {
        std::string reqName = REQ().GetTypeName();
        if(mHandlerMap.find(reqName) != mHandlerMap.end())
        {
            OnError(__FNAME__, __LINE__, "Failed to bind request " + reqName + ": it's already bound");
            return false;
        }
        mHandlerMap[reqName] = std::make_unique<HandlerImpl<SERVER, REQ, RESP>>((SERVER*)this, fptr);
        return true;
    }

private:
    struct Handler
    {
        virtual ~Handler() = default;
        virtual bool Call(const Context& ctx, const std::string& reqData, std::string& respData) = 0;
    };

    template<class SERVER, class REQ, class RESP>
    struct HandlerImpl : public Handler
    {
        typedef void (SERVER::*HANDLER_FPTR)(const Context& ctx, const REQ&, RESP&);
        HandlerImpl(SERVER* _srv, HANDLER_FPTR _fptr) : srv(_srv), fptr(_fptr) {}
        bool Call(const Context& ctx, const std::string& reqData, std::string& respData) override;
        SERVER* srv;
        HANDLER_FPTR fptr;
    };

    struct ClientContextImpl : public ClientContext
    {
        enum class MessageState { READING_CODE, READING_LEN, READING_DATA };
        MessageState messageState{MessageState::READING_CODE};
        
        PROTO_CODE currentCode{ACK};
        uint32_t expectedLen{0};

        Handler* handler{nullptr};
        std::string reqName;
        std::string reqData;

        void Reset()
        {
            messageState = MessageState::READING_CODE;
            handler = nullptr;
            reqName.clear();
            reqData.clear();
        }
    };

    std::shared_ptr<ClientContext> MakeClientContext() override final { return std::make_shared<ClientContextImpl>(); }
    bool OnDataReceived(std::shared_ptr<ClientContext>& clientIn) override final;

    void HandleFinishedFrame(std::shared_ptr<ClientContextImpl>& client, PROTO_CODE code, const std::string& data);
    void EnqueueProtoData(std::shared_ptr<ClientContextImpl>& client, PROTO_CODE code, const std::string& data);
    void EnqueueProtoCode(std::shared_ptr<ClientContextImpl>& client, PROTO_CODE code);

    bool ParseMetadata(const char* buffer, size_t bufferSize,
                       std::map<std::string, std::string>& data, std::string& errMsg);

private:
    std::map<const std::string, std::unique_ptr<Handler>> mHandlerMap;
};

inline bool ProtoServer::OnDataReceived(std::shared_ptr<EpollServer::ClientContext>& clientIn)
{
    auto client = std::static_pointer_cast<ClientContextImpl>(clientIn);
    
    while(true)
    {
        if(client->messageState == ClientContextImpl::MessageState::READING_CODE)
        {
            if(client->inboundBuffer.size() < sizeof(uint32_t)) 
                break; 
            
            uint32_t rawCode;
            std::memcpy(&rawCode, client->inboundBuffer.data(), sizeof(uint32_t));
            client->currentCode = static_cast<PROTO_CODE>(ntohl(rawCode));
            client->inboundBuffer.erase(client->inboundBuffer.begin(), client->inboundBuffer.begin() + sizeof(uint32_t));
            client->messageState = ClientContextImpl::MessageState::READING_LEN;
        }
        else if(client->messageState == ClientContextImpl::MessageState::READING_LEN)
        {
            if(client->inboundBuffer.size() < sizeof(uint32_t)) 
                break; 

            uint32_t rawLen;
            std::memcpy(&rawLen, client->inboundBuffer.data(), sizeof(uint32_t));
            client->expectedLen = ntohl(rawLen);
            client->inboundBuffer.erase(client->inboundBuffer.begin(), client->inboundBuffer.begin() + sizeof(uint32_t));
            client->messageState = ClientContextImpl::MessageState::READING_DATA;
        }
        else if(client->messageState == ClientContextImpl::MessageState::READING_DATA)
        {
            if(client->inboundBuffer.size() < client->expectedLen) 
                break; 

            std::string data(reinterpret_cast<char*>(client->inboundBuffer.data()), client->expectedLen);
            client->inboundBuffer.erase(client->inboundBuffer.begin(), client->inboundBuffer.begin() + client->expectedLen);
            
            HandleFinishedFrame(client, client->currentCode, data);
            client->messageState = ClientContextImpl::MessageState::READING_CODE;
        }
    }
    return true;
}

inline void ProtoServer::HandleFinishedFrame(std::shared_ptr<ClientContextImpl>& client, 
                                             PROTO_CODE code, const std::string& data)
{
    if(code == REQ_NAME)
    {
        client->reqName = data;
        auto itr = mHandlerMap.find(data);
        if(itr != mHandlerMap.end())
        {
            client->handler = itr->second.get();
            EnqueueProtoCode(client, ACK);
        }
        else
        {
            EnqueueProtoCode(client, NACK);
            EnqueueProtoData(client, ERR, "Unknown request: " + data);
            client->Reset();
        }
    }
    else if(code == REQ)
    {
        client->reqData = data;
    }
    else if(code == METADATA)
    {
        std::map<std::string, std::string> metadata;
        std::string errMsg;
        if(ParseMetadata(data.data(), data.size(), metadata, errMsg))
        {
            if(client->handler)
            {
                std::string respData;
                Context ctx(metadata);
                client->handler->Call(ctx, client->reqData, respData);
                EnqueueProtoData(client, RESP, respData);
                EnqueueProtoData(client, ERR, ctx.GetError());
            }
        }
        client->Reset();
    }
}

inline void ProtoServer::EnqueueProtoCode(std::shared_ptr<ClientContextImpl>& client, PROTO_CODE code)
{
    uint32_t val = htonl(static_cast<uint32_t>(code));
    client->Send(&val, sizeof(val));
}

inline void ProtoServer::EnqueueProtoData(std::shared_ptr<ClientContextImpl>& client, PROTO_CODE code, const std::string& data)
{
    EnqueueProtoCode(client, code);
    uint32_t len = htonl(static_cast<uint32_t>(data.length()));
    client->Send(&len, sizeof(len));
    if(data.length() > 0) 
        client->Send(data.data(), data.length());
}

template<class SERVER, class REQ, class RESP>
bool ProtoServer::HandlerImpl<SERVER, REQ, RESP>::Call(const ProtoServer::Context& ctx, const std::string& reqData, std::string& respData)
{
    REQ req;
    if(!req.ParseFromString(reqData)) 
        return false;
    RESP resp;
    (srv->*fptr)(ctx, req, resp);
    return resp.SerializeToString(&respData);
}

inline bool ProtoServer::ParseMetadata(const char* buffer, size_t bufferSize,
                                       std::map<std::string, std::string>& metadata, 
                                       std::string& errMsg)
{
    size_t offset = 0;
    auto CheckBuffer = [&](size_t needed) -> bool
    {
        if(offset + needed > bufferSize)
        {
            std::stringstream ss;
            ss << __FNAME__ << ":" << __LINE__ << " Unexpected end of buffer";
            errMsg = ss.str();
            return false;
        }
        return true;
    };

    if(!CheckBuffer(sizeof(uint32_t))) 
        return false;
    
    uint32_t sizeNetwork;
    std::memcpy(&sizeNetwork, buffer + offset, sizeof(sizeNetwork));
    uint32_t sizeHost = ntohl(sizeNetwork);
    offset += sizeof(sizeNetwork);

    std::map<std::string, std::string> strmap;
    for(uint32_t i = 0; i < sizeHost; ++i)
    {
        if(!CheckBuffer(sizeof(uint32_t))) 
            return false;
        
        uint32_t kLen;
        std::memcpy(&kLen, buffer + offset, sizeof(uint32_t));
        kLen = ntohl(kLen);
        offset += sizeof(uint32_t);

        if(!CheckBuffer(kLen)) 
            return false;
        
        std::string key(buffer + offset, kLen);
        offset += kLen;

        if(!CheckBuffer(sizeof(uint32_t))) 
            return false;
        
        uint32_t vLen;
        std::memcpy(&vLen, buffer + offset, sizeof(uint32_t));
        vLen = ntohl(vLen);
        offset += sizeof(uint32_t);

        if(!CheckBuffer(vLen)) 
            return false;
        
        std::string value(buffer + offset, vLen);
        offset += vLen;

        strmap[key] = value;
    }

    metadata = std::move(strmap);
    return true;
}

} // namespace gen

#endif // __PROTO_SERVER_HPP__