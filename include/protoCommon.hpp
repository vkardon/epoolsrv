//
// protoCommon.hpp
//
#ifndef __PROTO_COMMON_HPP__
#define __PROTO_COMMON_HPP__

#include "socketCommon.hpp"
#include <string>
#include <map>
#include <cstring>  // std::memcpy

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

inline bool ProtoSend(int sock, const void* buf, size_t len, long timeout_ms, std::string& errMsg)
{
    return Send(sock, buf, len, 0, timeout_ms, errMsg);
}

inline bool ProtoRecv(int sock, void* buf, size_t len, long timeout_ms, std::string& errMsg)
{
    return Recv(sock, buf, len, 0, timeout_ms, errMsg);
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
