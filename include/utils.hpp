//
// utils.hpp
//
#ifndef __UTILS_HPP__
#define __UTILS_HPP__

#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>

namespace gen {

    // Lightweight utility class designed to measure the execution time of a code block.
    // It captures a high-resolution timestamp upon instantiation and automatically
    // calculates and logs the elapsed duration in milliseconds to the standard output
    // when it goes out of scope.
    class StopWatch
    {
        std::chrono::time_point<std::chrono::high_resolution_clock> start;
        std::chrono::time_point<std::chrono::high_resolution_clock> stop;
        std::string prefix;

    public:
        StopWatch(const char* _prefix="") : prefix(_prefix)
        {
            start = std::chrono::high_resolution_clock::now();  
        }
        ~StopWatch()
        {
            stop = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> duration = stop - start;
            //std::cout << prefix << duration.count() << " sec" << std::endl;

            // Convert to a fixed-point duration (e.g., milliseconds)
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
            std::cout << prefix << duration_ms.count() << " ms" << std::endl;
        }
    };

    // "Hex Dump" function: Convert raw binary data into a human-readable hexadecimal string
    inline std::string ToHex(const void* str, int len)
    {
        const char* buf = static_cast<const char*>(str);
        std::stringstream hex_stream;
        hex_stream << std::hex << std::setfill('0');
        for(int i = 0; i < len; i++)
            hex_stream << std::setw(2) << static_cast<int>(buf[i]);
        return hex_stream.str();
    }

    inline std::string ToHex(const std::string& str) 
    { 
        return ToHex(str.data(), str.size()); 
    }

    // This constexpr method extracts the filename from a full path at compile time. 
    // It is intended for use with the __FILE__ macro and performs no argument validation.
    constexpr const char* GetFileName(const char* path) 
    {
        const char* lastSlash = path;
        for(const char* p = path; *p; ++p) 
        {
            if(*p == '/' || *p == '\\') 
                lastSlash = p + 1;
        }
        return lastSlash;
    }

} // namespace gen

// Macro that strips the directory path from __FILE__.
// Performed at compile-time using the constexpr gen::GetFileName helper.
#define __FNAME__ gen::GetFileName(__FILE__)

#endif // __UTILS_HPP__
