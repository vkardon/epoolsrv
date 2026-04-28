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
#include <poll.h>           // poll()
#include <arpa/inet.h>      // htonl()/ntohl()
#include <cstring>          // std::memcpy
#include <string>
#include <map>
#include <sstream>
#include <chrono>
#include "protoCommon.hpp"
#include "utils.hpp"

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

    // Send/Recv helpers
    bool ProtoSend(int sock, const void* buf, size_t len, long timeoutMs, std::string& errMsg) const;
    bool ProtoRecv(int sock, void* buf, size_t len, long timeoutMs, std::string& errMsg) const;
    bool ProtoSendInteger(int sock, uint32_t value, long timeout_ms, std::string& errMsg) const;
    bool ProtoRecvInteger(int sock, uint32_t& value, long timeout_ms, std::string& errMsg) const;
    bool ProtoSendCode(int sock, PROTO_CODE code, long timeout_ms, std::string& errMsg) const;
    bool ProtoValidateCode(uint32_t value, PROTO_CODE expectedCode, std::string& errMsg) const;
    bool ProtoRecvCode(int sock, PROTO_CODE code, long timeout_ms, std::string& errMsg) const;
    bool ProtoSendData(int sock, PROTO_CODE code, const std::string& data, long timeout_ms, std::string& errMsg) const;
    bool ProtoRecvData(int sock, PROTO_CODE code, std::string& data, long timeout_ms, std::string& errMsg) const;
    bool ProtoSendData(int sock, PROTO_CODE code, const std::map<std::string, std::string>& data,
                       long timeout_ms, std::string& errMsg) const;
    std::string SerializeToString(const std::map<std::string, std::string>& data) const;

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
    socklen_t addrLen = 0;

    // Check if the path starts with a null character or '@' (convention for abstract namespace).
    if(domainSocketPath[0] == '\0' || domainSocketPath[0] == '@')
    {
        // Abstract namespace socket. 
        // Ensure we don't overflow the buffer.
        const char* name = domainSocketPath + 1;
        size_t nameLen = strlen(name);
        if(nameLen > sizeof(serverAddress.sun_path) - 1) 
            nameLen = sizeof(serverAddress.sun_path) - 1;

        serverAddress.sun_path[0] = '\0';
        memcpy(serverAddress.sun_path + 1, name, nameLen);

        // Calculate precise length for abstract namespace
        addrLen = offsetof(struct sockaddr_un, sun_path) + 1 + nameLen;
    }
    else
    {
        // Regular domain socket
        strncpy(serverAddress.sun_path, domainSocketPath, sizeof(serverAddress.sun_path) - 1);
        addrLen = sizeof(struct sockaddr_un);
    }

    // Connect to the server
    if(connect(sock, (sockaddr*)&serverAddress, addrLen) == -1)
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

// If timeout is 0, then ProtoSend() will block until all the requested data is available.
// Returns: true if succeeded, false otherwise with errno set to:
//    ETIMEDOUT  - operation timed out
//    ECONNRESET - connection reset by peer
inline bool ProtoClient::ProtoSend(int sock, const void* buf, size_t len, long timeoutMs, std::string& errMsg) const
{
    // Set up for poll() to implement the timeout
    auto startTime = std::chrono::steady_clock::now();
    size_t totalSent = 0;

    while(totalSent < len)
    {
        if(timeoutMs > 0)
        {
            // Adjust timeout
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            auto remaining = std::chrono::milliseconds(timeoutMs) - elapsed;

            if(remaining <= std::chrono::microseconds(0))
            {
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " Timed out after " << timeoutMs << " ms";
                errMsg = std::move(ss.str());
                errno = ETIMEDOUT; // Timeout occurred
                return false;
            }

            // Convert remaining time to milliseconds for poll()
            long pollTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();

            // Use poll() to wait for writeability with a timeout
            pollfd fds[1];
            fds[0].fd = sock;
            fds[0].events = POLLOUT | POLLERR | POLLHUP; // Monitor for writeability
            fds[0].revents = 0;

            int retval = poll(fds, 1, pollTimeoutMs);

            if(retval == -1)
            {
                if(errno == EINTR)
                {
                    // poll() interrupted by signal. Retry poll() with adjusted timeout
                    continue;
                }
                else
                {
                    std::stringstream ss;
                    ss << __FNAME__ << ":" << __LINE__ << " poll() failed: " << strerror(errno);
                    errMsg = std::move(ss.str());
                    return false; // Error in poll
                }
            }
            else if(retval == 0)
            {
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " Timed out after " << timeoutMs << " ms";
                errMsg = std::move(ss.str());
                errno = ETIMEDOUT; // Timeout occurred
                return false;
            }

            if(fds[0].revents & (POLLERR | POLLHUP))
            {
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " Connection error detected via poll";
                errMsg = ss.str();
                errno = ECONNRESET;
                return false;
            }
            // OK, socket is writeable. Let's write to the socket
            else if(!(fds[0].revents & POLLOUT))
            {
                // No POLLIN event, but poll returned > 0 (shouldn't happen in this simple read case)
                // You might want to log a warning or handle other revents if needed
                continue;
            }
        }

        ssize_t bytesSent = send(sock, static_cast<const char*>(buf) + totalSent, len - totalSent, 0 /*flags*/);

        if(bytesSent == -1)
        {
            if(errno == EINTR)
            {
                // send() interrupted by signal. Retry send() (poll will be retried too)
                continue;
            }
            else if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Socket is non-blocking, try again later
                continue;
            }
            else if(errno == EPIPE || errno == ECONNRESET)
            {
                // Connection likely closed by peer
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " Connection closed by peer: " << strerror(errno);
                errMsg = std::move(ss.str());
                errno = ECONNRESET; // Indicate connection closure
                return false;
            }
            else
            {
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " send() failed: " << strerror(errno);
                errMsg = std::move(ss.str());
                return false;
            }
        }
        else if(bytesSent > 0)
        {
            totalSent += bytesSent;
        }
    }

    return true;
}

// If timeout is 0, then ProtoRecv() will block until all the requested data is available.
// Returns: true if succeeded, false otherwise with errno set to:
//    ETIMEDOUT  - operation timed out
//    ECONNRESET - connection reset by peer
//    ENOTCONN   - socket that is not connected
inline bool ProtoClient::ProtoRecv(int sock, void* buf, size_t len, 
                                   long timeoutMs, std::string& errMsg) const
{
    // Set up for poll() to implement the timeout
    auto startTime = std::chrono::steady_clock::now();
    size_t totalReceived = 0;

    while(totalReceived < len)
    {
        if(timeoutMs > 0)
        {
            // Adjust timeout
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            auto remaining = std::chrono::milliseconds(timeoutMs) - elapsed;

            if(remaining <= std::chrono::microseconds(0))
            {
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " Timed out after " << timeoutMs << " ms";
                errMsg = std::move(ss.str());
                errno = ETIMEDOUT; // Timeout occurred
                return false;
            }

            // Convert remaining time to milliseconds for poll()
            long pollTimeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();

            // Use poll() to wait for readability with a timeout
            pollfd fds[1];
            fds[0].fd = sock;
            fds[0].events = POLLIN | POLLERR | POLLHUP; // Monitor for readability
            fds[0].revents = 0;

            int retval = poll(fds, 1, pollTimeoutMs);

            if(retval == -1)
            {
                if(errno == EINTR)
                {
                    // poll() interrupted by signal. Retry poll() with adjusted timeout
                    continue;
                }
                else
                {
                    std::stringstream ss;
                    ss << __FNAME__ << ":" << __LINE__ << " poll() failed: " << strerror(errno);
                    errMsg = std::move(ss.str());
                    return false; // Error in poll
                }
            }
            else if(retval == 0)
            {
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " Timed out after " << timeoutMs << " ms";
                errMsg = std::move(ss.str());
                errno = ETIMEDOUT; // Timeout occurred
                return false;
            }

            if(fds[0].revents & (POLLERR | POLLHUP))
            {
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " Connection error detected via poll";
                errMsg = std::move(ss.str());
                errno = ECONNRESET;
                return false;
            }
            // OK, socket is readable. Let's read from the socket
            else if(!(fds[0].revents & POLLIN))
            {
                // No POLLIN event, but poll returned > 0 (shouldn't happen in this simple read case)
                // You might want to log a warning or handle other revents if needed
                continue;
            }
        }

        ssize_t bytesReceived = recv(sock, static_cast<char*>(buf) + totalReceived, len - totalReceived, 0 /*flags*/);

        if(bytesReceived == -1)
        {
            if(errno == EINTR)
            {
                // recv() interrupted by signal. Retry recv() (poll will be retried too)
                continue;
            }
            else if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No data available yet, but the call would not have blocked indefinitely.
                // We should continue to the next iteration of the poll loop to wait for data
                continue;
            }
            else
            {
                std::stringstream ss;
                ss << __FNAME__ << ":" << __LINE__ << " recv() failed: " << strerror(errno);
                errMsg = std::move(ss.str());
                return false;
            }
        }
        else if(bytesReceived == 0)
        {
            std::stringstream ss;
            if(totalReceived == 0)
            {
                ss << __FNAME__ << ":" << __LINE__ << " Socket is not connected (recv returned 0)";
                errno = ENOTCONN;   // Indicate that socket is not connected
            }
            else
            {
                ss << __FNAME__ << ":" << __LINE__ << " Connection closed by peer (recv returned 0)";
                errno = ECONNRESET; // Indicate connection closure
            }
            errMsg = std::move(ss.str());
            return false;
        }
        else
        {
            // Successfully received data
            totalReceived += bytesReceived;
        }
    }

    return true;
}

inline bool ProtoClient::ProtoSendInteger(int sock, uint32_t value, 
                                          long timeout_ms, std::string& errMsg) const
{
    uint32_t data = htonl(value);
    return ProtoSend(sock, &data, sizeof(data), timeout_ms, errMsg);
}

inline bool ProtoClient::ProtoRecvInteger(int sock, uint32_t& value, 
                                          long timeout_ms, std::string& errMsg) const
{
    uint32_t data = 0;
    bool res = ProtoRecv(sock, &data, sizeof(data), timeout_ms, errMsg);
    if(res)
        value = ntohl(data);
    return res;
}

inline bool ProtoClient::ProtoSendCode(int sock, PROTO_CODE code, 
                                       long timeout_ms, std::string& errMsg) const
{
    return ProtoSendInteger(sock, code, timeout_ms, errMsg);
}

inline bool ProtoClient::ProtoValidateCode(uint32_t value, PROTO_CODE expectedCode, 
                                           std::string& errMsg) const
{
    // Validate proto code
    if(value != expectedCode)
    {
        std::stringstream ss;
        ss << "Received " << ProtoCodeToStr((PROTO_CODE)value) << " (" << value << ") "
              "instead of " << ProtoCodeToStr(expectedCode) << " (" << expectedCode << ")";
        errMsg = std::move(ss.str());
        return false;
    }

    return true;
}

inline bool ProtoClient::ProtoRecvCode(int sock, PROTO_CODE code, 
                                       long timeout_ms, std::string& errMsg) const
{
    // Receive proto code from the client
    uint32_t value = 0;

    if(!ProtoRecvInteger(sock, value, timeout_ms, errMsg))
        return false;

    // Validate proto code
    if(!ProtoValidateCode(value, code, errMsg))
        return false;

    return true;
}

inline bool ProtoClient::ProtoSendData(int sock, PROTO_CODE code, const std::string& data, 
                                       long timeout_ms, std::string& errMsg) const
{
    // Sent the data proto code
    if(!ProtoSendCode(sock, code, timeout_ms, errMsg))
        return false;

    // Send the data size
    if(!ProtoSendInteger(sock, data.length(), timeout_ms, errMsg))
        return false;

    // Send the data itself (if no empty)
    if(data.length() > 0)
    {
        if(!ProtoSend(sock, data.data(), data.length(), timeout_ms, errMsg))
            return false;
    }

    return true;
}

inline bool ProtoClient::ProtoRecvData(int sock, PROTO_CODE code, std::string& data,
                                       long timeout_ms, std::string& errMsg) const
{
    // Receive the data code
    if(!ProtoRecvCode(sock, code, timeout_ms, errMsg))
        return false;

    // Receive the data length
    uint32_t len = 0;
    if(!ProtoRecvInteger(sock, len, timeout_ms, errMsg))
        return false;

    // Receive the data (if available)
    data.clear();
    if(len > 0)
    {
        // Receive the data itself
        data.reserve(len);   // Reserve the desired capacity
        data.resize(len);    // Set the correct size of the string

        if(!ProtoRecv(sock, data.data(), len, timeout_ms, errMsg))
            return false;
    }

    return true;
}

inline std::string ProtoClient::SerializeToString(const std::map<std::string, std::string>& data) const
{
    // 1. Calculate the required capacity
    size_t requiredCapacity = sizeof(uint32_t); // Size of the map
    for(const auto& pair : data)
    {
        requiredCapacity += sizeof(uint32_t) + pair.first.length(); // Key length + key data
        requiredCapacity += sizeof(uint32_t) + pair.second.length(); // Value length + value data
    }

    // 2. Create the vector and reserve the capacity
    std::string buffer;
    buffer.reserve(requiredCapacity);

    // 3. Write the number of key-value pairs (size of the map) in network byte order
    uint32_t sizeHost = data.size();
    uint32_t sizeNetwork = htonl(sizeHost);
    buffer.insert(buffer.end(), (char*) &sizeNetwork,(char*)&sizeNetwork + sizeof(sizeNetwork));

    // 4. Iterate through each key-value pair and append to the buffer
    for(const auto &pair : data)
    {
        // Append the key:
        uint32_t keyLenHost = pair.first.length();
        uint32_t keyLenNetwork = htonl(keyLenHost);
        buffer.insert(buffer.end(), (char*)&keyLenNetwork, (char*)&keyLenNetwork + sizeof(keyLenNetwork));
        buffer.insert(buffer.end(), pair.first.begin(), pair.first.end());

        // Append the value:
        uint32_t valueLenHost = pair.second.length();
        uint32_t valueLenNetwork = htonl(valueLenHost);
        buffer.insert(buffer.end(), (char*)&valueLenNetwork, (char*)&valueLenNetwork + sizeof(valueLenNetwork));
        buffer.insert(buffer.end(), pair.second.begin(), pair.second.end());
    }

    return buffer;
}

inline bool ProtoClient::ProtoSendData(int sock, PROTO_CODE code, 
                                       const std::map<std::string, std::string>& data,
                                       long timeout_ms, std::string& errMsg) const
{
    std::string buffer = SerializeToString(data);
    return ProtoSendData(sock, code, buffer, timeout_ms, errMsg);
}

} // namespace gen

#endif // __PROTO_CLIENT_HPP__

