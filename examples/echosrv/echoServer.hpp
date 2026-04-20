//
// echoServer.hpp
//
#ifndef __ECHO_SERVER_HPP__
#define __ECHO_SERVER_HPP__

#include "epollServer.hpp"

class EchoServer : public gen::EpollServer
{
public:
    EchoServer(size_t threadsCount) : gen::EpollServer(threadsCount) {}
    EchoServer() = delete;
    ~EchoServer() override = default;

private:
    struct ClientContextImpl : public ClientContext
    {
        std::string inputBuffer;    // Persistent storage across OnRead calls
        std::string outputBuffer;   // For outgoing data
    };

    bool OnRead(std::shared_ptr<ClientContext>& client) override;
    bool OnWrite(std::shared_ptr<ClientContext>& client) override;
 
    std::shared_ptr<ClientContext> MakeClientContext() override;
 
    void OnError(const char* fname, int lineNum, const std::string& err) const override;
    void OnInfo(const char* fname, int lineNum, const std::string& info) const override;

    std::string MakeResponse(const std::string& inputStr);
};

inline std::shared_ptr<EchoServer::ClientContext> EchoServer::MakeClientContext()
{
    return std::make_shared<ClientContextImpl>();
}

inline void EchoServer::OnError(const char* fname, int lineNum, const std::string& err) const
{
    std::cerr << fname << ":" << lineNum << " " << err << std::endl;
}

inline void EchoServer::OnInfo(const char* fname, int lineNum, const std::string& info) const
{
    std::cout << fname << ":" << lineNum << " " << info << std::endl;
}

inline bool EchoServer::OnRead(std::shared_ptr<ClientContext>& clientIn)
{
    // Recover our persistent state
    auto client = std::static_pointer_cast<ClientContextImpl>(clientIn);
    char buf[4096]{};
    
    // Non-blocking read (pull whatever is available right now)
    ssize_t bytesRead = recv(client->fd, buf, sizeof(buf), 0);

    if(bytesRead < 0)
    {
        // If the socket is non-blocking, EAGAIN means "no more data for now"
        if(errno == EAGAIN || errno == EWOULDBLOCK) 
            return true;
        
        OnError(__FNAME__, __LINE__, std::string("recv error: ") + strerror(errno));
        return false;
    }
    else if(bytesRead == 0)
    {
        return false; // Connection closed by peer
    }

    // Store the chunk in the persistent buffer
    client->inputBuffer.append(buf, bytesRead);

    // Search for the terminator
    size_t pos = client->inputBuffer.find('\n');
    if(pos != std::string::npos)
    {
        // We have a full message!
        std::string request = client->inputBuffer.substr(0, pos);
        
        // Handle \r\n if necessary
        if(!request.empty() && request.back() == '\r') 
            request.pop_back();

        // Process the message
        if(mVerbose)
        {
            std::stringstream ss;
            //ss << "Connection " << client->connectionId << " sent " << request.length() << " bytes.";
            ss << "Connection " << client->connectionId << " sent '" << request << "'.";
            OnInfo(__FNAME__, __LINE__, ss.str());
        }

        // Add response to the outputBuffer
        client->outputBuffer = MakeResponse(request);
        client->wantsWrite = true; // Signal the base class to add EPOLLOUT

        // Clean the persistent buffer for the NEXT chunk
        client->inputBuffer.erase(0, pos + 1);
    }
    else
    {
        // No newline found yet. We return true, which allows the base class
        // to re-arm epoll. When more data arrives, OnRead will be called again
        // and we will continue where we left off.
        if(mVerbose)
            OnInfo(__FNAME__, __LINE__, "Partial chunk received, waiting for newline...");
    }

    return true;
}

inline bool EchoServer::OnWrite(std::shared_ptr<ClientContext>& clientIn)
{
    // Recover our persistent state
    auto client = std::static_pointer_cast<ClientContextImpl>(clientIn);

    // If there is nothing to send, we just return true.
    // The base class will then switch us back to EPOLLIN mode.
    if(client->outputBuffer.empty()) 
    {
        client->wantsWrite = false; // Buffer empty, stop asking for EPOLLOUT
        return true;
    }

    // Try to send as much as the kernel will take
    // Note: Use MSG_NOSIGNAL to don't get SIGPIPE signal on closed connection
    ssize_t bytesSent = send(client->fd, 
                             client->outputBuffer.data(), 
                             client->outputBuffer.size(), 
                             MSG_NOSIGNAL);

    if(bytesSent > 0)
    {
        // Remove the data that was actually sent from our buffer
        client->outputBuffer.erase(0, bytesSent);
    }
    else if(bytesSent < 0)
    {
        // If the kernel buffer is full, just return true and stay in EPOLLOUT
        if(errno == EAGAIN || errno == EWOULDBLOCK) 
        {
            client->wantsWrite = true;    // Keep EPOLLOUT active
            return true;
        } 

        // Real socket error (e.g., Connection Reset)
        OnError(__FNAME__, __LINE__, "send() failed: " + std::string(strerror(errno)));
        return false;
    }

    // If the outputBuffer is still NOT empty (partial send),
    // we need to stay in EPOLLOUT mode.
    if(client->outputBuffer.empty())
        client->wantsWrite = false; // Buffer empty, stop asking for EPOLLOUT
    else
        client->wantsWrite = true;  // Still data left, keep EPOLLOUT active

    return true;
}

std::string EchoServer::MakeResponse(const std::string& request) 
{
    // Generate the hash
    std::hash<std::string> hasher;
    size_t hashValue = hasher(request);
    
    // std::hex     : convert to hex
    // std::setw    : sets the width (16 for a 64-bit size_t)
    // std::setfill : pads with '0' if the number is small
    std::stringstream ss;
    ss << std::hex << std::setw(sizeof(size_t) * 2) << std::setfill('0') << hashValue << '\n';
    return ss.str();
}

#endif // __ECHO_SERVER_HPP__
