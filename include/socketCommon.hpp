//
// socketCommon.hpp
//
#ifndef __SOCKET_COMMON_HPP__
#define __SOCKET_COMMON_HPP__

#include <unistd.h>
#include <sys/socket.h>
#include <poll.h>           // poll()
#include <arpa/inet.h>      // htonl()/ntohl()
#include <sys/un.h>
#include <string.h>         // strerror()
#include <string>
#include <sstream>
#include <chrono>

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

inline int SetupServerSocket(unsigned short port, bool nonblocking, 
                             int backlog, std::string& errMsg)
{
    // Create socket
    int sock = socket(AF_INET, (nonblocking ? SOCK_STREAM | SOCK_NONBLOCK : SOCK_STREAM), 0);
    if(sock == -1)
    {
        errMsg = "socket() failed: " + std::string(strerror(errno));
        return -1;
    }

    int reuse = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        close(sock);
        errMsg = "setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno));
        return -1;
    }

    // Bind the socket
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if(bind(sock, (sockaddr*) &serverAddress, sizeof(serverAddress)) == -1)
    {
        close(sock);
        errMsg = "bind() failed: " + std::string(strerror(errno));
        return -1;
    }

    // Listen for connections
    if(listen(sock, backlog) == -1)
    {
        close(sock);
        errMsg = "listen() failed: " + std::string(strerror(errno));
        return -1;
    }

    return sock;
}

inline int SetupServerDomainSocket(const char* sockName, bool isAbstract, 
                                   int backlog, bool nonblocking, std::string& errMsg)
{
    if(!sockName || *sockName == '\0')
    {
        errMsg = "Socket creation failed: invalid (empty) socket name";
        return -1;
    }

    // Create socket
    int sock = socket(AF_UNIX, (nonblocking ? SOCK_STREAM | SOCK_NONBLOCK : SOCK_STREAM), 0);
    if(sock == -1)
    {
        errMsg = "socket() failed: " + std::string(strerror(errno));
        return -1;
    }

    // Prepare the server address structure for a Regular (Filesystem)
    // or Abstract Namespace Unix Domain Socket.
    sockaddr_un serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sun_family = AF_UNIX;

    if(isAbstract)
    {
        // Abstract namespace socket
        serverAddress.sun_path[0] = 0;
        strncpy(serverAddress.sun_path + 1, sockName, sizeof(serverAddress.sun_path) - 2);
    }
    else
    {
        // Regular (Filesystem) socket
        strncpy(serverAddress.sun_path, sockName, sizeof(serverAddress.sun_path) - 1);
    }

    // Unlink any existing socket with the same name (optional but good practice).
    // This doesn't affect filesystem paths if this in the abstract namespace.
    // However, it's a good idea to ensure a clean state if the program crashed previously.
    unlink(serverAddress.sun_path);

    // Bind the socket
    if(bind(sock, (sockaddr*) &serverAddress, sizeof(serverAddress)) == -1)
    {
        close(sock);
        errMsg = "bind() failed: " + std::string(strerror(errno));
        return -1;
    }

    // Listen for connections
    if(listen(sock, backlog) == -1)
    {
        close(sock);
        errMsg = "listen() failed: " + std::string(strerror(errno));
        return -1;
    }

    return sock;
}

inline int SetupClientSocket(const char* host, int port, std::string& errMsg)
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

inline int SetupClientDomainSocket(const char* domainSocketPath, std::string& errMsg)
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

// If timeout is 0, then Recv() will block until all the requested data is available.
// Returns: true if succeeded, false otherwise with errno set to:
//    ETIMEDOUT  - operation timed out
//    ECONNRESET - connection reset by peer
//    ENOTCONN   - socket that is not connected
inline bool Recv(int sock, void* buf, size_t len, int flags, long timeoutMs, std::string& errMsg)
{
    // Set up for poll() to implement the timeout
    auto startTime = std::chrono::steady_clock::now();
    auto timeoutDuration = std::chrono::milliseconds(timeoutMs);

    pollfd fds[1];
    fds[0].fd = sock;
    fds[0].events = POLLIN; // Monotor for readability
    fds[0].revents = 0;

    size_t totalReceived = 0;

    while(totalReceived < len)
    {
        if(timeoutMs > 0)
        {
            // Adjust timeout
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - startTime;
            auto remaining = timeoutDuration - elapsed;

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

            // OK, socket is readable. Let's read from the socket
            if(!(fds[0].revents & POLLIN))
            {
                // No POLLIN event, but poll returned > 0 (shouldn't happen in this simple read case)
                // You might want to log a warning or handle other revents if needed
                continue;
            }
        }

        ssize_t bytesReceived = recv(sock, static_cast<char*>(buf) + totalReceived, len - totalReceived, flags);

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

// If timeout is 0, then Send() will block until all the requested data is available.
// Returns: true if succeeded, false otherwise with errno set to:
//    ETIMEDOUT  - operation timed out
//    ECONNRESET - connection reset by peer
inline bool Send(int sock, const void* buf, size_t len, int flags, long timeoutMs, std::string& errMsg)
{
    // Set up for poll() to implement the timeout
    auto startTime = std::chrono::steady_clock::now();
    auto timeoutDuration = std::chrono::milliseconds(timeoutMs);

    pollfd fds[1];
    fds[0].fd = sock;
    fds[0].events = POLLOUT; // Monitor for writeability
    fds[0].revents = 0;

    size_t totalSent = 0;

    while(totalSent < len)
    {
        if(timeoutMs > 0)
        {
            // Adjust timeout
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - startTime;
            auto remaining = timeoutDuration - elapsed;

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

            // OK, socket is writeable. Let's write to the socket
            if(!(fds[0].revents & POLLOUT))
            {
                // No POLLIN event, but poll returned > 0 (shouldn't happen in this simple read case)
                // You might want to log a warning or handle other revents if needed
                continue;
            }
        }

        ssize_t bytesSent = send(sock, static_cast<const char*>(buf) + totalSent, len - totalSent, flags);

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

} // namespace gen

#endif // __SOCKET_COMMON_HPP__
