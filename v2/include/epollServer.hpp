#ifndef __EPOLL_SERVER_HPP__
#define __EPOLL_SERVER_HPP__

#include <iostream>
#include <sys/epoll.h>
#include <sys/un.h> 
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <cstring>
#include "utils.hpp"

const int DEFAULT_BACKLOG = 1024;
const int DEFAULT_MAX_EVENTS = 1024;
const int MAX_ACCEPTS_PER_WAKEUP = 512; 
const unsigned long MAX_CONNECTION_IDLE_TIME = 60;
const size_t MAX_OUTBOUND_BUFFER_SIZE = 10 * 1024 * 1024;

namespace gen {

class EpollServer
{
public:
    EpollServer(unsigned int threadsCount) : mThreadsCount(threadsCount) {}
    virtual ~EpollServer() { Stop(); }

    bool Start(unsigned short port, int backlog = DEFAULT_BACKLOG);
    bool Start(const char* unixPath, int backlog = DEFAULT_BACKLOG);
    void Stop();

    void SetVerbose(bool verbose) { mVerbose = verbose; }

protected:
    struct ClientContext
    {
        ClientContext() = default;
        virtual ~ClientContext() = default;

        enum class RecvStatus { UNKNOWN=0, OK, DISCONNECT, ERROR };

        bool Send(const void* data, size_t len) 
        {
            if(outboundBuffer.size() + len > MAX_OUTBOUND_BUFFER_SIZE)
                return false;

            const uint8_t* p = static_cast<const uint8_t*>(data);
            outboundBuffer.insert(outboundBuffer.end(), p, p + len);
            wantsWrite = true;
            return true;
        }

        RecvStatus Receive(std::string& errMsg)
        {
            char buf[4096];
            RecvStatus status = RecvStatus::UNKNOWN;

            while(true)
            {
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if(n > 0)
                {
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(buf);
                    inboundBuffer.insert(inboundBuffer.end(), p, p + n);
                    continue; // Continue reading until EAGAIN
                }
                else if(n == 0)
                {
                    // Peer closed, but we might have data in inboundBuffer!
                    status = RecvStatus::DISCONNECT;
                    break;
                }
                else if(errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    status = RecvStatus::OK;
                    break;
                }
                else if(errno == EINTR)
                {
                    continue;
                }
                else
                {
                    errMsg = strerror(errno);
                    status = RecvStatus::ERROR;
                    break;
                }
            }

            return status;
        }

        int fd{-1};
        std::chrono::time_point<std::chrono::steady_clock> lastActivityTime;
        uint64_t connectionId{0};
        std::atomic<bool> wantsWrite{false};
        std::vector<uint8_t> inboundBuffer;
        std::vector<uint8_t> outboundBuffer;
        size_t outOffset{0}; 
    };

    virtual bool OnInit() { return true; }
    virtual bool OnDataReceived(std::shared_ptr<ClientContext>& client) = 0;
    virtual bool OnDataSent(std::shared_ptr<ClientContext>& client) { return true; }
    virtual std::shared_ptr<ClientContext> MakeClientContext() = 0;
    virtual void OnError(const char* fname, int lineNum, const std::string& err) const;
    virtual void OnInfo(const char* fname, int lineNum, const std::string& info) const;

private:
    void ReactorLoop();
    int SetupTcpSocket(unsigned short port, int backlog, std::string& errMsg);
    int SetupUnixSocket(const char* unixPath, int backlog, std::string& errMsg);
    bool IsUnixSocket() const { return (mUnixDomainSocket >= 0); }
    bool IsTcpSocket() const { return !IsUnixSocket(); }
    bool FlushOutboundBuffer(std::shared_ptr<ClientContext>& client);
    int GetMaxFiles();

private:
    unsigned int mThreadsCount{0};
    std::atomic<bool> mServerRunning{false};
    std::atomic<uint64_t> mNextConnectionId{1};
    std::vector<std::thread> mThreads;
    
    unsigned short mPort{0};
    int mUnixDomainSocket{-1}; 
    std::string mActiveUnixPath;
    int mBacklog{DEFAULT_BACKLOG};

protected:
    bool mVerbose{false};
};

inline int EpollServer::GetMaxFiles()
{
    struct rlimit rl;
    if(getrlimit(RLIMIT_NOFILE, &rl) == 0)
        return static_cast<int>(rl.rlim_cur);
    return 65535;
}

inline bool EpollServer::Start(unsigned short port, int backlog)
{
    if(!OnInit())
    {
        OnError(__FNAME__, __LINE__, "Initialization failed: OnInit() returned false");
        return false;
    }

    mPort = port;
    mBacklog = backlog;
    mServerRunning = true;

    std::stringstream ss;
    ss << "Starting Server on port " << port << " with " << mThreadsCount << " threads...";
    OnInfo(__FNAME__, __LINE__, ss.str());

    for(unsigned int i = 0; i < mThreadsCount; ++i)
        mThreads.emplace_back([this]() { ReactorLoop(); });

    return true;
}

inline bool EpollServer::Start(const char* unixPath, int backlog)
{
    if(!OnInit())
    {
        OnError(__FNAME__, __LINE__, "Initialization failed: OnInit() returned false");
        return false;
    }
    
    std::string errMsg;
    mUnixDomainSocket = SetupUnixSocket(unixPath, backlog, errMsg);
    if(mUnixDomainSocket < 0)
    {
        OnError(__FNAME__, __LINE__, errMsg);
        return false;
    }
    mBacklog = backlog;
    mServerRunning = true;

    std::stringstream ss;
    ss << "Starting Server on Unix Domain Socket '" << mActiveUnixPath << "' with " << mThreadsCount << " threads...";
    OnInfo(__FNAME__, __LINE__, ss.str());

    for(unsigned int i = 0; i < mThreadsCount; ++i)
        mThreads.emplace_back([this]() { ReactorLoop(); });

    return true;
}

inline void EpollServer::Stop() 
{ 
    mServerRunning = false; 

    for(auto& t : mThreads)
    {
        if(t.joinable()) 
            t.join();
    }
    mThreads.clear();

    if(IsUnixSocket())
    {
        if(mUnixDomainSocket >= 0)
            close(mUnixDomainSocket);
        if(!mActiveUnixPath.empty())
            unlink(mActiveUnixPath.c_str());
        mUnixDomainSocket = -1;
    }
}

inline void EpollServer::ReactorLoop()
{
    int threadEpollFd = epoll_create1(EPOLL_CLOEXEC);
    if(threadEpollFd == -1)
    {
        OnError(__FNAME__, __LINE__, "epoll_create1() failed: " + std::string(strerror(errno)));
        return;
    }

    int maxFds = GetMaxFiles();
    std::vector<std::shared_ptr<ClientContext>> localClients(maxFds, nullptr);

    int listenFd = (IsUnixSocket() ? mUnixDomainSocket : -1);
    if(listenFd == -1)
    {
        std::string errMsg;
        listenFd = SetupTcpSocket(mPort, mBacklog, errMsg);
        if(listenFd < 0)
        {
            OnError(__FNAME__, __LINE__, errMsg);
            close(threadEpollFd);
            return;
        }
    }

    struct epoll_event ev;
    ev.events = (IsUnixSocket() ? EPOLLIN : (EPOLLIN | EPOLLET)); 
    ev.data.u64 = (static_cast<uint64_t>(1) << 63) | (static_cast<uint32_t>(listenFd));
    
    if(epoll_ctl(threadEpollFd, EPOLL_CTL_ADD, listenFd, &ev) == -1)
    {
        OnError(__FNAME__, __LINE__, "epoll_ctl(listenFd) failed: " + std::string(strerror(errno)));
        if(IsTcpSocket())
            close(listenFd);
        close(threadEpollFd);
        return;
    }

    struct epoll_event events[DEFAULT_MAX_EVENTS];
    auto lastCleanupTime = std::chrono::steady_clock::now();

    while(mServerRunning)
    {
        int numEvents = epoll_wait(threadEpollFd, events, DEFAULT_MAX_EVENTS, 100);
        if(numEvents < 0)
        {
            if(errno == EINTR)
                continue;
            break;
        }

        for(int i = 0; i < numEvents; ++i)
        {
            uint64_t eventData = events[i].data.u64;
            bool isListener = static_cast<bool>(eventData >> 63);

            if(isListener)
            {
                int lfd = static_cast<int>(eventData & 0xFFFFFFFF);
                int acceptCount = 0;
                while(mServerRunning && acceptCount < MAX_ACCEPTS_PER_WAKEUP)
                {
                    int clientFd = accept4(lfd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if(clientFd == -1)
                        break;

                    if(clientFd >= maxFds)
                    {
                        close(clientFd);
                        continue;
                    }

                    acceptCount++;
                    auto ctx = MakeClientContext();
                    ctx->fd = clientFd;
                    ctx->connectionId = mNextConnectionId.fetch_add(1, std::memory_order_relaxed);
                    ctx->lastActivityTime = std::chrono::steady_clock::now();
                    
                    localClients[clientFd] = ctx;

                    struct epoll_event cev;
                    cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                    cev.data.u64 = static_cast<uint64_t>(clientFd);

                    if(epoll_ctl(threadEpollFd, EPOLL_CTL_ADD, clientFd, &cev) == -1)
                    {
                        close(clientFd);
                        localClients[clientFd] = nullptr;
                    }
                    else if(mVerbose)
                        OnInfo(__FNAME__, __LINE__, "Accepted connection ID: " + std::to_string(ctx->connectionId));
                }
            }
            else
            {
                int clientFd = static_cast<int>(eventData);
                auto& client = localClients[clientFd];
                if(!client)
                    continue;

                bool keepAlive = true;
                client->lastActivityTime = std::chrono::steady_clock::now();

                if(events[i].events & EPOLLOUT)
                {
                    keepAlive = FlushOutboundBuffer(client);
                    if(keepAlive && !client->wantsWrite)
                        keepAlive = OnDataSent(client);
                }

                if(keepAlive && (events[i].events & EPOLLIN))
                {
                    std::string recvErr;
                    auto status = client->Receive(recvErr);

                    // Always process data if we have it, regardless of status
                    if(!client->inboundBuffer.empty())
                    {
                        keepAlive = OnDataReceived(client);
                    }

                    // If Receive signaled a disconnect or termination state, mark for closure
                    if(status == ClientContext::RecvStatus::DISCONNECT)
                    {
                        keepAlive = false;
                    }
                    else if(status == ClientContext::RecvStatus::ERROR)
                    {
                        OnError(__FNAME__, __LINE__, "Receive error: " + recvErr);
                        keepAlive = false;                    
                    }
                    
                    // Flush any responses before the loop potentially closes the FD
                    if(client->wantsWrite)
                    {
                        if(!FlushOutboundBuffer(client))
                            keepAlive = false;
                    }
                }

                if(!keepAlive || (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)))
                {
                    epoll_ctl(threadEpollFd, EPOLL_CTL_DEL, clientFd, nullptr);
                    close(clientFd);
                    localClients[clientFd] = nullptr;
                }
                else
                {
                    struct epoll_event cev;
                    cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
                    if(client->wantsWrite)
                        cev.events |= EPOLLOUT;
                    cev.data.u64 = static_cast<uint64_t>(clientFd);

                    if(epoll_ctl(threadEpollFd, EPOLL_CTL_MOD, clientFd, &cev) == -1)
                    {
                        close(clientFd);
                        localClients[clientFd] = nullptr;
                    }
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        if(now - lastCleanupTime > std::chrono::seconds(5))
        {
            for(int fd = 0; fd < maxFds; ++fd)
            {
                if(localClients[fd] && (now - localClients[fd]->lastActivityTime > std::chrono::seconds(MAX_CONNECTION_IDLE_TIME)))
                {
                    if(mVerbose)
                        OnInfo(__FNAME__, __LINE__, "Closing idle connection ID: " + std::to_string(localClients[fd]->connectionId));
                    
                    epoll_ctl(threadEpollFd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    localClients[fd] = nullptr;
                }
            }
            lastCleanupTime = now;
        }
    }

    for(auto& client : localClients)
    {
        if(client)
            close(client->fd);
    }
    if(IsTcpSocket() && listenFd >= 0)
        close(listenFd);
    close(threadEpollFd);
}

inline bool EpollServer::FlushOutboundBuffer(std::shared_ptr<ClientContext>& client)
{
    while(client->outOffset < client->outboundBuffer.size())
    {
        ssize_t n = send(client->fd, client->outboundBuffer.data() + client->outOffset, 
                         client->outboundBuffer.size() - client->outOffset, MSG_NOSIGNAL);
        if(n > 0)
            client->outOffset += n;
        else
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                return true;
            return false;
        }
    }
    client->outboundBuffer.clear();
    client->outOffset = 0;
    client->wantsWrite = false;
    return true;
}

inline int EpollServer::SetupTcpSocket(unsigned short port, int backlog, std::string& errMsg)
{
    int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if(sock == -1) 
    {
        errMsg = "socket() failed: " + std::string(strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(sock, (sockaddr*)&addr, sizeof(addr)) == -1)
    {
        errMsg = "bind() failed: " + std::string(strerror(errno));
        close(sock);
        return -1;
    }

    if(listen(sock, backlog) == -1)
    {
        errMsg = "listen() failed: " + std::string(strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

inline int EpollServer::SetupUnixSocket(const char* unixPath, int backlog, std::string& errMsg)
{
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    socklen_t addrLen = 0;

    if(unixPath[0] == '@' || unixPath[0] == '\0')
    {
        const char* name = unixPath + 1;
        size_t nameLen = strlen(name);
        addr.sun_path[0] = '\0';
        std::memcpy(addr.sun_path + 1, name, nameLen);
        addrLen = offsetof(struct sockaddr_un, sun_path) + 1 + nameLen;
        mActiveUnixPath.assign(name, nameLen);
    }
    else
    {
        unlink(unixPath);
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", unixPath);
        addrLen = sizeof(struct sockaddr_un);
        mActiveUnixPath = unixPath;
    }

    int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if(sock == -1) 
    {
        errMsg = "socket(AF_UNIX) failed: " + std::string(strerror(errno));
        return -1;
    }

    if(bind(sock, (struct sockaddr*)&addr, addrLen) == -1)
    {
        errMsg = "bind(UDS) failed: " + std::string(strerror(errno));
        close(sock);
        return -1;
    }

    if(listen(sock, backlog) == -1)
    {
        errMsg = "listen(UDS) failed: " + std::string(strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

inline void EpollServer::OnError(const char* fname, int lineNum, const std::string& err) const 
{
    std::cerr << "Error: " << fname << ":" << lineNum << " " << err << std::endl;
}

inline void EpollServer::OnInfo(const char* fname, int lineNum, const std::string& info) const 
{
    std::cout << "Info: " << fname << ":" << lineNum << " " << info << std::endl;
}

} // namespace gen

#endif  // __EPOLL_SERVER_HPP__