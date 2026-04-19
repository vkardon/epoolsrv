//
// protoClient.cpp
//
#ifndef __PROTO_CLIENT_HPP__
#define __PROTO_CLIENT_HPP__

#include <unistd.h>
#include <sys/socket.h>
#include <string.h>         // strerror()
#include <sys/un.h>
#include <vector>
#include <google/protobuf/message.h>
#include "protoCommon.hpp"

namespace gen {

class ProtoClient
{
public:
    ProtoClient() = default;
    ProtoClient(const char* domainSocketPath) { Init(domainSocketPath, mErrMsg); }
    ProtoClient(const char* host, unsigned short port) { Init(host, port, mErrMsg); }
    ~ProtoClient();

    bool Init(const char* domainSocketPath, std::string& errMsg);
    bool Init(const char* host, unsigned short port, std::string& errMsg);
    bool IsValid() { return (mSocket > 0); }

    // Call with metadata
    bool Call(const google::protobuf::Message& req,
              google::protobuf::Message& resp,
              const std::map<std::string, std::string>& metadata,
              std::string& errMsg,
              long timeoutMs = 5000);

    // No medatada call
    bool Call(const google::protobuf::Message& req,
              google::protobuf::Message& resp,
              std::string& errMsg,
              long timeoutMs = 5000);

private:
    int SetupClientSocket(const char* host, int port, std::string& errMsg);
    int SetupClientDomainSocket(const char* domainSocketPath, std::string& errMsg);

    int mSocket{-1};
    std::string mErrMsg;
};

inline ProtoClient::~ProtoClient()
{
    if(mSocket > 0)
        close(mSocket);
}

inline bool ProtoClient::Init(const char* domainSocketPath, std::string& errMsg)
{
    return ((mSocket = SetupClientDomainSocket(domainSocketPath, errMsg)) > 0);
}

inline bool ProtoClient::Init(const char* host, unsigned short port, std::string& errMsg)
{
    return ((mSocket = SetupClientSocket(host, port, errMsg)) > 0);
}

inline int ProtoClient::SetupClientSocket(const char* host, int port, std::string& errMsg)
{
    if(!host || *host == '\0')
    {
        errMsg = "Socket creation failed: Invalid (empty) host name";
        return -1;
    }
    else if(port == 0)
    {
        errMsg = "Socket creation failed: Invalid (zero) port number";
        return -1;
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1)
    {
        errMsg = std::string("socket() failed: ") + strerror(errno);
        return -1;
    }

    // Prepare server address
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    if(inet_pton(AF_INET, host, &serverAddress.sin_addr) <= 0)
    {
        close(sock);
        errMsg = std::string("inet_pton() failed: ") + strerror(errno);
        return -1;
    }

    // Connect to the server
    if(connect(sock, (sockaddr*) &serverAddress, sizeof(serverAddress)) == -1)
    {
        close(sock);
        errMsg = std::string("connect() failed: ") + strerror(errno);
        return -1;
    }

    return sock;
}

inline int ProtoClient::SetupClientDomainSocket(const char* domainSocketPath, std::string& errMsg)
{
    // Create a socket
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sock == -1)
    {
        errMsg = std::string("socket() failed: ") + strerror(errno);
        return -1;
    }

    // Prepare server address in the abstract namespace
    sockaddr_un serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sun_family = AF_UNIX;

    if(*domainSocketPath == '\0')
    {
        // Abstract namespace socket
        serverAddress.sun_path[0] = 0;
        strncpy(serverAddress.sun_path + 1, domainSocketPath + 1, sizeof(serverAddress.sun_path) - 2);
    }
    else
    {
        // Regular domain socket
        strncpy(serverAddress.sun_path, domainSocketPath, sizeof(serverAddress.sun_path) - 1);
    }

    // Connect to the server
    if(connect(sock, (sockaddr*)&serverAddress, sizeof(serverAddress)) == -1)
    {
        close(sock);
        errMsg = std::string("connect() failed: ") + strerror(errno);
        return -1;
    }

    return sock;
}

// Call with metadata
inline bool ProtoClient::Call(const google::protobuf::Message& req,
                              google::protobuf::Message& resp,
                              const std::map<std::string, std::string>& metadata,
                              std::string& errMsgOut,
                              long timeoutMs)
{
    if(timeoutMs == 0)
        timeoutMs = 3'600'000; // One hour default timeout

    try
    {
        if(mSocket < 0)
            throw (!mErrMsg.empty() ? mErrMsg : std::string("Invalid socket (-1)"));

        // Do we have non-empty request message?
        // Note: it's OK to send an empty request.
        std::string reqName = req.GetTypeName();
        std::string reqData;
        if(size_t reqSize = req.ByteSizeLong(); reqSize > 0)
        {
            // Serialize request protobuf message to string
            if(!req.SerializeToString(&reqData))
                throw std::string("Failed to write protobuf request message, size=") + std::to_string(reqSize);
        }

        // Call the server
        std::string errMsg;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        long remainingTimeoutMs = timeoutMs;

        // Sent the REQ_NAME (request name)
        if(!ProtoSendData(mSocket, PROTO_CODE::REQ_NAME, reqName, remainingTimeoutMs, errMsg))
            throw std::string("Failed to send REQ_NAME (request name): ") + errMsg;

        // Adjust timeout
        auto remaining = deadline - std::chrono::steady_clock::now();
        if(remaining <= std::chrono::microseconds(0))
            throw std::string("Timed out after ") + std::to_string(timeoutMs) + " ms";
        remainingTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();

        // Expecting ACK or NACK back from server
        uint32_t code = 0;
        if(!ProtoRecvInteger(mSocket, code, remainingTimeoutMs, errMsg))
            throw std::string("Failed to receive ACK/NACK code: ") + errMsg;

        // Adjust timeout
        remaining = deadline - std::chrono::steady_clock::now();
        if(remaining <= std::chrono::microseconds(0))
            throw std::string("Timed out after ") + std::to_string(timeoutMs) + " ms";
        remainingTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();

        if(code == PROTO_CODE::NACK)
        {
            // Receive ERR (error message)
            if(!ProtoRecvData(mSocket, PROTO_CODE::ERR, errMsgOut, remainingTimeoutMs, errMsg))
                throw std::string("Failed to receive ERR (response value): ") + errMsg;

            // Note: Don't throw because it will close the socket; just return false
            return false;
        }
        else if(code != PROTO_CODE::ACK)
        {
            throw std::string("Failed to receive ACK/NACK code, received ") + std::to_string(code) + " instead";
        }

        // Send the REQ (request data)
        if(!ProtoSendData(mSocket, PROTO_CODE::REQ, reqData, remainingTimeoutMs, errMsg))
            throw std::string("Failed to send REQ (request data): ") + errMsg;

        // Adjust timeout
        remaining = deadline - std::chrono::steady_clock::now();
        if(remaining <= std::chrono::microseconds(0))
            throw std::string("Timed out after ") + std::to_string(timeoutMs) + " ms";
        remainingTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();

        // Send metadata
        if(!ProtoSendData(mSocket, PROTO_CODE::METADATA, metadata, remainingTimeoutMs, errMsg))
            throw std::string("Failed to send METADATA: ") + errMsg;

        // Adjust timeout
        remaining = deadline - std::chrono::steady_clock::now();
        if(remaining <= std::chrono::microseconds(0))
            throw std::string("Timed out after ") + std::to_string(timeoutMs) + " ms";
        remainingTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();

        // Receive RESP (response data)
        std::string respData;
        if(!ProtoRecvData(mSocket, PROTO_CODE::RESP, respData, remainingTimeoutMs, errMsg))
            throw std::string("Failed to receive RESP (respData data): ") + errMsg;

        // Adjust timeout
        remaining = deadline - std::chrono::steady_clock::now();
        if(remaining <= std::chrono::microseconds(0))
            throw std::string("Timed out after ") + std::to_string(timeoutMs) + " ms";
        remainingTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();

        // Receive ERR (error message)
        if(!ProtoRecvData(mSocket, PROTO_CODE::ERR, errMsgOut, remainingTimeoutMs, errMsg))
            throw std::string("Failed to receive ERR (response value): ") + errMsg;

        // Adjust timeout
        remaining = deadline - std::chrono::steady_clock::now();
        if(remaining <= std::chrono::microseconds(0))
            throw std::string("Timed out after ") + std::to_string(timeoutMs) + " ms";

        // Create protobuf message from the response data
        if(!resp.ParseFromString(respData))
            throw std::string("Failed to parse response data into protobuf message ") +
                     resp.GetTypeName() + " with size: " + std::to_string(respData.length());

        return true;
    }
    catch(const std::string& e)
    {
        errMsgOut = std::string(__func__) + ": " + e;
    }
    catch(const std::exception& ex)
    {
        errMsgOut = std::string(__func__) + ": std::exception: " + ex.what();
    }
    catch(...)
    {
        errMsgOut = std::string(__func__) + ": Unexpected exception";
    }

    close(mSocket);
    mSocket = -1;
    return false;
}

// No metadata call
inline bool ProtoClient::Call(const google::protobuf::Message& req,
                              google::protobuf::Message& resp,
                              std::string& errMsg,
                              long timeoutMs)
{
    return Call(req, resp, std::map<std::string, std::string>(), errMsg, timeoutMs);
}

} // namespace gen

#endif // __PROTO_CLIENT_HPP__

