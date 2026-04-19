//
// socketCommon.hpp
//
#ifndef __SOCKET_COMMON_HPP__
#define __SOCKET_COMMON_HPP__

// victor test - for debugging
//#include <iomanip>
//inline std::string ToHex(const void* str, int len)
//{
//    const char* buf = static_cast<const char*>(str);
//    std::stringstream hex_stream;
//    hex_stream << std::hex << std::setfill('0');
//    for(int i = 0; i < len; i++)
//      hex_stream << std::setw(2) << static_cast<int>(buf[i]);
//    return hex_stream.str();
//}
//inline std::string ToHex(const std::string& str) { return ToHex(str.data(), str.size()); }
//
//class StopWatch
//{
//    std::chrono::time_point<std::chrono::high_resolution_clock> start;
//    std::chrono::time_point<std::chrono::high_resolution_clock> stop;
//    std::string prefix;
//
//public:
//    StopWatch(const char* _prefix="") : prefix(_prefix)
//    {
//        start = std::chrono::high_resolution_clock::now();
//    }
//    ~StopWatch()
//    {
//        stop = std::chrono::high_resolution_clock::now();
//        std::chrono::duration<double> duration = stop - start;
////        std::cout << prefix << duration.count() << " sec" << std::endl;
//
//        // Option 1: Convert to a fixed-point duration (e.g., milliseconds)
//        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
//        std::cout << prefix << duration_ms.count() << " ms" << std::endl;
//    }
//};
// victor test end

namespace gen {

#ifndef __FNAME__
    // This constexpr method extracts the filename from a full path at compile time. 
    // It is intended for use with the __FILE__ macro and performs no argument validation.
    constexpr const char* fname(const char* file, int i)
    {
        return (i == 0) ? (file) : (*(file + i) == '/' ? (file + i + 1) : fname(file, i - 1));
    }
    #define __FNAME__ gen::fname(__FILE__, sizeof(__FILE__)-1)
#endif

} // namespace gen

#endif // __SOCKET_COMMON_HPP__
