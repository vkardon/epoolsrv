//
// epoollServer.hpp
//
#ifndef __EPOLL_SERVER_HPP__
#define __EPOLL_SERVER_HPP__

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "socketCommon.hpp"
#include "threadPool.hpp"

const int DEFAULT_BACKLOG = 512;
const int DEFAULT_MAX_CONNECTIONS = 4096;
const int DEFAULT_MAX_EVENTS = 64;
const int DEFAULT_IDLE_TIMEOUT = 60;    // Sec

namespace gen {

class EpollServer
{
public:
    EpollServer(unsigned int threadsCount) : mThreadsCount(threadsCount) {}
    virtual ~EpollServer() { Stop(); }

    bool Start(unsigned short port, int backlog = DEFAULT_BACKLOG);
    bool Start(const char* sockName, bool isAbstract, int backlog = DEFAULT_BACKLOG);

    bool AddListener(unsigned short port, int backlog = DEFAULT_BACKLOG);
    bool AddListener(const char* sockName, bool isAbstract, int backlog = DEFAULT_BACKLOG);
    bool Start();
    void Stop() { mServerRunning = false; }

    // Configuration
    void SetMaxEpollEventsCount(int maxEvents) { mMaxEvents = maxEvents; }
    void SetMaxConnections(int maxConnections) { mMaxConnections = maxConnections; }
    void SetIdleTimeout(int timeoutSec) { mIdleTimeout = std::chrono::seconds(timeoutSec); }
    void SetVerbose(bool verbose) { mVerbose = verbose; }

protected:
    struct ClientContext
    {
        ClientContext() = default;
        virtual ~ClientContext() = default;

        int fd{-1};
        std::chrono::time_point<std::chrono::steady_clock> lastActivityTime;
        int connectionId{0};
    };

    // For derived class to override
    virtual bool OnInit() { return true; }
    virtual bool OnRead(std::shared_ptr<ClientContext>& client) = 0;
    virtual bool OnWrite(std::shared_ptr<ClientContext>& client) = 0;
    virtual std::shared_ptr<ClientContext> MakeClientContext() = 0;
    virtual void OnError(const char* fname, int lineNum, const std::string& err) const;
    virtual void OnInfo(const char* fname, int lineNum, const std::string& info) const;

private:
    bool CanAcceptNewConnection();
    void UpdateActivityTime(int clientFd);
    void CheckIdleConnections();
    void HandleAcceptEvent(int listenFd);
    void HandleReadEvent(int clientFd);
    void HandleWriteEvent(int clientFd);
    void CleanupClient(int clientFd);
    void Cleanup();

    void AddClientContext(int clientFd, const struct sockaddr_in& clientAddr);
    std::shared_ptr<ClientContext> GetClientContext(int clientFd);

    bool EpollAdd(int fd, uint32_t events, bool isListener);
    bool EpollMod(int fd, uint32_t events);
    bool EpollDel(int fd);

    // No default or copy constructors
    EpollServer() = delete;
    EpollServer(const EpollServer&) = delete;

private:
    unsigned int mThreadsCount{0};
    int mMaxEvents{DEFAULT_MAX_EVENTS};
    std::chrono::seconds mIdleTimeout{DEFAULT_IDLE_TIMEOUT};
    size_t mMaxConnections{DEFAULT_MAX_CONNECTIONS};
    std::atomic<bool> mServerRunning{false};
    int mEpollFd{-1};
    std::vector<int> mListenersFds;
    std::atomic<int> mNextConnectionId{1};
    std::map<int, std::shared_ptr<ClientContext>> mClientContexts;
    std::mutex mClientContextsMutex;
    ThreadPool mThreadPool;

protected:
    bool mVerbose{false};

};

inline bool EpollServer::Start(unsigned short port, int backlog)
{
    return AddListener(port, backlog) && Start();
}

inline bool EpollServer::Start(const char* sockName, bool isAbstract, int backlog)
{
    return AddListener(sockName, isAbstract, backlog) && Start();
}

inline bool EpollServer::AddListener(unsigned short port, int backlog)
{
    // Create listening NET socket (nonblocking)
    std::string errMsg;
    int listenFd = SetupServerSocket(port, false /*blocking*/, backlog, errMsg);
    if(listenFd < 0)
    {
        OnError(__FNAME__, __LINE__, errMsg);
        return false;
    }
    mListenersFds.push_back(listenFd);

    std::stringstream ss;
    ss << "Starting server on port " << port << ".";
    OnInfo(__FNAME__, __LINE__, ss.str());
    return true;
}

inline bool EpollServer::AddListener(const char* sockName, bool isAbstract, int backlog)
{
    // Create listening unix domain socket (nonblocking)
    std::string errMsg;
    int listenFd = SetupServerDomainSocket(sockName, isAbstract, false /*blocking*/, backlog, errMsg);
    if(listenFd < 0)
    {
        OnError(__FNAME__, __LINE__, errMsg);
        return false;
    }
    mListenersFds.push_back(listenFd);

    std::stringstream ss;
    ss << "Starting server on domain socket" << (isAbstract ? " in abstract namespace " : " ") << "'" << sockName << "'.";
    OnInfo(__FNAME__, __LINE__, ss.str());
    return true;
}

inline bool EpollServer::Start()
{
    if(!OnInit())
    {
        OnError(__FNAME__, __LINE__, "Initialization failed: OnInit() returned false");
        Cleanup();
        return false;
    }

    // Create epoll instance
    mEpollFd = epoll_create1(0);
    if(mEpollFd == -1)
    {
        OnError(__FNAME__, __LINE__, "epoll_create1() failed: " + std::string(strerror(errno)));
        Cleanup();
        return false;
    }

    // Add listening sockets to epoll
    for(int listenFd : mListenersFds)
    {
        if(!EpollAdd(listenFd, EPOLLIN, true /*listener socket*/))
        {
            OnError(__FNAME__, __LINE__, "Error adding listening fd " + std::to_string(listenFd) + " to epoll.");
            Cleanup();
            return false;
        }
    }

    OnInfo(__FNAME__, __LINE__, "Starting thread pool with " + std::to_string(mThreadsCount) + " worker threads.");

    // Start worker threads
    mThreadPool.Start(mThreadsCount);

    // Main event loop
    mServerRunning = true;
    struct epoll_event events[mMaxEvents];
    int epollWaitTimeoutMs = 100;

    auto lastIdleCheck = std::chrono::steady_clock::now();
    auto idleCheckInterval = std::chrono::seconds(5); // Check every 5 seconds

    while(mServerRunning)
    {
        int numEvents = epoll_wait(mEpollFd, events, mMaxEvents, epollWaitTimeoutMs);

        if(numEvents > 0)
        {
            for(int i = 0; i < numEvents; ++i)
            {
                uint32_t event = events[i].events;

                // Unpack event.data.u64 field to the socket file descriptor and listener boollean flag:
                // - If the highest bit is set, it's a listener
                // - Erase the highest bit (boolean flag bit) to unpack the socket
                bool isListener = static_cast<bool>(events[i].data.u64 >> 63);
                int fd = static_cast<int>(events[i].data.u64 & 0x7FFFFFFFFFFFFFFF); // Erase the highest bit

                if(isListener)
                {
                    HandleAcceptEvent(fd);
                }
                else
                {
                    // Queue a task for a worker thread to handle this event
                    if(event & (EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLERR))
                    {
                        mThreadPool.Post(&EpollServer::HandleReadEvent, this, fd);
                    }
                    else if(event & EPOLLOUT)
                    {
                        mThreadPool.Post(&EpollServer::HandleWriteEvent, this, fd);
                    }
                }
            }
        }
        else if(numEvents == 0) // Timeout occurred
        {
            // Periodically check for idle connections
            auto now = std::chrono::steady_clock::now();
            if(now - lastIdleCheck > idleCheckInterval)
            {
                CheckIdleConnections();
                lastIdleCheck = now;
            }
        }
        else if(numEvents == -1 && errno != EINTR)
        {
            OnError(__FNAME__, __LINE__, "epoll_wait() failed in main loop: " + std::string(strerror(errno)));
        }
    }

    OnInfo(__FNAME__, __LINE__, "Main event loop finished.");
    Cleanup();
    OnInfo(__FNAME__, __LINE__, "Epoll server stopped.");
    return true;
}

inline void EpollServer::Cleanup()
{
    // Stop the thread pool and wait all threads to complete
    mThreadPool.Stop();
    mThreadPool.Wait();

    // Note: We don't need to lock mClientContextsMutex since threads are gone
    for(const auto& pair : mClientContexts)
        close(pair.first); // pair.first is the key (fd)

    mClientContexts.clear();

    if(mEpollFd != -1)
    {
        close(mEpollFd);
        mEpollFd = -1;
    }

    for(int listenFd : mListenersFds)
    {
        close(listenFd);
    }
    mListenersFds.clear();
}

inline bool EpollServer::EpollAdd(int fd, uint32_t events, bool isListener)
{
    struct epoll_event event;
    event.events = events;

    // Use event.data.u64 field to pack both the socket and listener boollean flag.
    // Set the least significant bits for the socket file descriptor and 
    // the highest bit for the boolean flag.
    event.data.u64 = static_cast<uint64_t>(isListener) << 63 | static_cast<uint64_t>(fd);

    if(epoll_ctl(mEpollFd, EPOLL_CTL_ADD, fd, &event) == -1)
    {
        OnError(__FNAME__, __LINE__, "epoll_ctl(EPOLL_CTL_ADD) failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

inline bool EpollServer::EpollMod(int fd, uint32_t events)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;

    if(epoll_ctl(mEpollFd, EPOLL_CTL_MOD, fd, &event) == -1)
    {
        OnError(__FNAME__, __LINE__, "epoll_ctl(EPOLL_CTL_MOD) failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

inline bool EpollServer::EpollDel(int fd)
{
    if(epoll_ctl(mEpollFd, EPOLL_CTL_DEL, fd, nullptr) == -1)
    {
        OnError(__FNAME__, __LINE__, "epoll_ctl(EPOLL_CTL_DEL) failed: " + std::string(strerror(errno)));
        return false;
    }
    return true;
}

inline bool EpollServer::CanAcceptNewConnection()
{
    std::lock_guard<std::mutex> lock(mClientContextsMutex);
    return (mClientContexts.size() < mMaxConnections);
}

inline void EpollServer::AddClientContext(int clientFd, const struct sockaddr_in &clientAddr)
{
    std::shared_ptr<ClientContext> client = MakeClientContext();
    client->fd = clientFd;
    client->lastActivityTime = std::chrono::steady_clock::now();
    client->connectionId = mNextConnectionId++;

    {
        std::lock_guard<std::mutex> lock(mClientContextsMutex);
        mClientContexts[clientFd] = client;
    }

    std::string clientIp = inet_ntoa(clientAddr.sin_addr);
    unsigned short clientPort = ntohs(clientAddr.sin_port);

    if(mVerbose)
    {
        std::stringstream ss;
        ss << "Connection " << client->connectionId << " from " << clientIp
           << ":" << clientPort << " accepted, clientFd=" << clientFd << ".";
        OnInfo(__FNAME__, __LINE__, ss.str());
    }
}

inline std::shared_ptr<EpollServer::ClientContext> EpollServer::GetClientContext(int clientFd)
{
    std::lock_guard<std::mutex> lock(mClientContextsMutex);
    auto it = mClientContexts.find(clientFd);
    return (it != mClientContexts.end() ? it->second : nullptr);
}

inline void EpollServer::UpdateActivityTime(int clientFd)
{
    std::lock_guard<std::mutex> lock(mClientContextsMutex);
    if(mClientContexts.count(clientFd))
    {
        mClientContexts[clientFd]->lastActivityTime = std::chrono::steady_clock::now();
    }
}

inline void EpollServer::CheckIdleConnections()
{
    auto now = std::chrono::steady_clock::now();
    std::vector<int> clientsToClose;

    {
        std::lock_guard<std::mutex> lock(mClientContextsMutex);
        for(const auto &pair : mClientContexts)
        {
            if((now - pair.second->lastActivityTime) > mIdleTimeout)
            {
                if(mVerbose)
                {
                    std::stringstream ss;
                    ss << "Closing idle connection " << pair.second->connectionId << " (fd " << pair.first << ").";
                    OnInfo(__FNAME__, __LINE__, ss.str());
                }

                clientsToClose.push_back(pair.first);
            }
        }
    }

    for(int fd : clientsToClose)
        CleanupClient(fd);
}

inline void EpollServer::HandleAcceptEvent(int listenFd)
{
    sockaddr_in clientAddr;
    socklen_t clientAddressLen = sizeof(clientAddr);
    int connFd = accept(listenFd, (sockaddr*)&clientAddr, &clientAddressLen);
    if(connFd == -1)
    {
        OnError(__FNAME__, __LINE__, std::string("Accept failed: ") + strerror(errno));
        return;
    }

    if(CanAcceptNewConnection())
    {
        AddClientContext(connFd, clientAddr);

        if(!EpollAdd(connFd, EPOLLIN | EPOLLRDHUP | EPOLLONESHOT, false /*connection socket*/))
        {
            OnError(__FNAME__, __LINE__, "Error adding client fd " + std::to_string(connFd) + " to epoll.");
            close(connFd);
            std::lock_guard<std::mutex> lock(mClientContextsMutex);
            mClientContexts.erase(connFd);
        }
    }
    else
    {
        std::stringstream ss;
        ss << "Maximum connections reached. Rejecting new connection from "
           << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port);
        OnError(__FNAME__, __LINE__, ss.str());
        close(connFd); // Immediately close the connection
    }
}

inline void EpollServer::HandleReadEvent(int clientFd)
{
    std::shared_ptr<ClientContext> client = GetClientContext(clientFd);
    if(!client)
    {
        std::stringstream ss;
        ss << "Client context not found for fd " << clientFd << " in read event.";
        OnError(__FNAME__, __LINE__, ss.str());
        return;
    }

    if(!OnRead(client))
    {
        CleanupClient(clientFd);
        return;
    }

    UpdateActivityTime(clientFd);

    // Immediately modify epoll to listen for EPOLLOUT
    if(!EpollMod(clientFd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLONESHOT))
    {
        std::stringstream ss;
        ss << "Error modifying epoll for fd " << clientFd << " to include EPOLLOUT.";
        OnError(__FNAME__, __LINE__, ss.str());
        CleanupClient(clientFd);
    }
}

inline void EpollServer::HandleWriteEvent(int clientFd)
{
    std::shared_ptr<ClientContext> client = GetClientContext(clientFd);
    if(!client)
    {
        std::stringstream ss;
        ss << "Client context not found for fd " << clientFd << " in read event.";
        OnError(__FNAME__, __LINE__, ss.str());
        return;
    }

    if(!OnWrite(client))
    {
        CleanupClient(clientFd);
        return;
    }

    UpdateActivityTime(clientFd);

    // Immediately modify epoll to listen for EPOLLIN again
    if(!EpollMod(clientFd, EPOLLIN | EPOLLRDHUP | EPOLLONESHOT))
    {
        std::stringstream ss;
        ss << "Error modifying epoll for fd " << clientFd << " back to EPOLLIN.";
        OnError(__FNAME__, __LINE__, ss.str());
        CleanupClient(clientFd);
    }
}

inline void EpollServer::CleanupClient(int clientFd)
{
    {
        std::unique_lock<std::mutex> lock(mClientContextsMutex);
        auto it = mClientContexts.find(clientFd);

        if(it == mClientContexts.end())
        {
            lock.unlock();
            std::stringstream ss;
            ss << "Client context not found for fd " << clientFd << " in read event.";
            OnError(__FNAME__, __LINE__, ss.str());
        }
        else
        {
            std::shared_ptr<ClientContext> client = it->second;

            if(mVerbose)
            {
                std::stringstream ss;
                ss << "Closing connection " << client->connectionId  << " (fd " << clientFd << ").";
                OnInfo(__FNAME__, __LINE__, ss.str());
            }

            mClientContexts.erase(clientFd);
        }
    }

    if(!EpollDel(clientFd))
    {
        OnError(__FNAME__, __LINE__, "Error removing fd " + std::to_string(clientFd) + " from epoll.");
    }

    close(clientFd);
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

#endif // __EPOLL_SERVER_HPP__

