//
// protoCommon.hpp
//
#ifndef __PROTO_COMMON_HPP__
#define __PROTO_COMMON_HPP__

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

} // namespace gen

#endif // __PROTO_COMMON_HPP__
