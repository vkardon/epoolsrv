//
// protoCommon.hpp
//
#ifndef __PROTO_COMMON_HPP__
#define __PROTO_COMMON_HPP__

#include <sys/socket.h>
#include <poll.h>           // poll()
#include <arpa/inet.h>      // htonl()/ntohl()
#include <string.h>         // strerror()
#include <string>
#include <map>
#include <cstring>          // std::memcpy
#include <sstream>
#include <chrono>
#include "utils.hpp"

namespace gen {

// All communication codes supported by ProtoServer
enum PROTO_CODE : uint32_t
{
    ACK = 1000,
    NACK,
    REQ_NAME,
    REQ,
    RESP,
    METADATA,
    ERR
};

inline const char* ProtoCodeToStr(PROTO_CODE code)
{
    return (code == ACK       ? "ACK" :
            code == NACK      ? "NACK" :
            code == REQ_NAME  ? "REQ_NAME" :
            code == REQ       ? "REQ" :
            code == RESP      ? "RESP" :
            code == ERR       ? "ERR" : "UNKNOWN");
}

// If timeout is 0, then ProtoSend() will block until all the requested data is available.
// Returns: true if succeeded, false otherwise with errno set to:
//    ETIMEDOUT  - operation timed out
//    ECONNRESET - connection reset by peer
inline bool ProtoSend(int sock, const void* buf, size_t len, long timeoutMs, std::string& errMsg)
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
inline bool ProtoRecv(int sock, void* buf, size_t len, long timeoutMs, std::string& errMsg)
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

inline bool ProtoSendInteger(int sock, uint32_t value, long timeout_ms, std::string& errMsg)
{
    uint32_t data = htonl(value);
    return ProtoSend(sock, &data, sizeof(data), timeout_ms, errMsg);
}

inline bool ProtoRecvInteger(int sock, uint32_t& value, long timeout_ms, std::string& errMsg)
{
    uint32_t data = 0;
    bool res = ProtoRecv(sock, &data, sizeof(data), timeout_ms, errMsg);
    if(res)
        value = ntohl(data);
    return res;
}

inline bool ProtoSendCode(int sock, PROTO_CODE code, long timeout_ms, std::string& errMsg)
{
    return ProtoSendInteger(sock, code, timeout_ms, errMsg);
}

inline bool ProtoValidateCode(uint32_t value, PROTO_CODE expectedCode, std::string& errMsg)
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

inline bool ProtoRecvCode(int sock, PROTO_CODE code, long timeout_ms, std::string& errMsg)
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

inline bool ProtoSendData(int sock, PROTO_CODE code, const std::string& data, long timeout_ms, std::string& errMsg)
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

inline bool ProtoRecvData(int sock, PROTO_CODE code, std::string& data, long timeout_ms, std::string& errMsg)
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

inline std::string SerializeToString(const std::map<std::string, std::string>& data)
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

inline bool ParseFromData(const char* buffer, size_t bufferSize,
                          std::map<std::string, std::string>& data, std::string& errMsg)
{
    std::map<std::string, std::string> strmap;
    size_t offset = 0;

    auto CheckBuffer = [&](size_t needed) -> bool
    {
        if(offset + needed > bufferSize)
        {
            std::stringstream ss;
            ss << __FNAME__ << ":" << __LINE__ << " Unexpected end of buffer while deserializing";
            errMsg = std::move(ss.str());
            return false;
        }
        return true;
    };

    // 1. Read the number of key-value pairs in network byte order and convert to host
    if(!CheckBuffer(sizeof(uint32_t)))
        return false;

    uint32_t sizeNetwork;
    std::memcpy(&sizeNetwork, buffer + offset, sizeof(sizeNetwork));
    uint32_t sizeHost = ntohl(sizeNetwork);
    offset += sizeof(sizeNetwork);

    // 2. Iterate and deserialize each key-value pair
    for(uint32_t i = 0; i < sizeHost; ++i)
    {
        // Deserialize the key length (network to host)
        if(!CheckBuffer(sizeof(uint32_t)))
            return false;

        uint32_t keyLenNetwork;
        std::memcpy(&keyLenNetwork, buffer + offset,
                sizeof(keyLenNetwork));
        uint32_t keyLenHost = ntohl(keyLenNetwork);
        offset += sizeof(keyLenNetwork);

        if(!CheckBuffer(keyLenHost))
            return false;

        std::string key(buffer + offset, keyLenHost);
        offset += keyLenHost;

        // Deserialize the value length (network to host)
        if(!CheckBuffer(sizeof(uint32_t)))
            return false;

        uint32_t valueLenNetwork;
        std::memcpy(&valueLenNetwork, buffer + offset, sizeof(valueLenNetwork));
        uint32_t valueLenHost = ntohl(valueLenNetwork);
        offset += sizeof(valueLenNetwork);

        if(!CheckBuffer(valueLenHost))
            return false;

        std::string value(buffer + offset, valueLenHost);
        offset += valueLenHost;

        strmap[key] = value;
    }

    if(offset != bufferSize)
    {
        std::stringstream ss;
        ss << __FNAME__ << ":" << __LINE__ << " Buffer contains extra data after deserialization";
        errMsg = std::move(ss.str());
        return false;
    }

    data = std::move(strmap);
    return true;
}

inline bool ProtoSendData(int sock, PROTO_CODE code, const std::map<std::string, std::string>& data,
                          long timeout_ms, std::string& errMsg)
{
    std::string buffer = SerializeToString(data);
    return ProtoSendData(sock, code, buffer, timeout_ms, errMsg);
}

inline bool ProtoRecvData(int sock, PROTO_CODE code, std::map<std::string, std::string>& data,
                          long timeout_ms, std::string& errMsg)
{
    std::string buffer;
    if(!ProtoRecvData(sock, code, buffer, timeout_ms, errMsg))
        return false;

    return ParseFromData(buffer.data(), buffer.size(), data, errMsg);
}

} // namespace gen

#endif // __PROTO_COMMON_HPP__
