//
// hashServer.hpp
//
#ifndef __HASH_SERVER_HPP__
#define __HASH_SERVER_HPP__

#include "epollServer.hpp"
#include <openssl/evp.h>

class HashServer : public gen::EpollServer
{
public:
    HashServer(size_t threadsCount) : gen::EpollServer(threadsCount) {}
    HashServer() = delete;
    ~HashServer() override = default;

private:
    struct ClientContextImpl : public ClientContext
    {
        ClientContextImpl() 
        {
            hashCtx = EVP_MD_CTX_new();
            EVP_DigestInit_ex(hashCtx, EVP_sha256(), nullptr);
        }

        ~ClientContextImpl() override 
        {
            if(hashCtx) 
                EVP_MD_CTX_free(hashCtx);
        }

        EVP_MD_CTX* hashCtx{nullptr}; // Persistent hash state
        std::string outputBuffer;     // For outgoing hex strings
    };

    std::shared_ptr<gen::EpollServer::ClientContext> MakeClientContext() override;

    bool OnRead(std::shared_ptr<ClientContext>& client) override;
    bool OnWrite(std::shared_ptr<ClientContext>& client) override;
 
    void OnError(const char* fname, int lineNum, const std::string& err) const override;
    void OnInfo(const char* fname, int lineNum, const std::string& info) const override;

    void AppendHex(std::string& dest, const void* hash, int len) const;
};

inline std::shared_ptr<gen::EpollServer::ClientContext> HashServer::MakeClientContext()
{
    return std::make_shared<ClientContextImpl>();
}

inline void HashServer::OnError(const char* fname, int lineNum, const std::string& err) const
{
    std::cerr << fname << ":" << lineNum << " " << err << std::endl;
}

inline void HashServer::OnInfo(const char* fname, int lineNum, const std::string& info) const
{
    std::cout << fname << ":" << lineNum << " " << info << std::endl;
}

inline bool HashServer::OnRead(std::shared_ptr<ClientContext>& clientIn)
{
    // Recover our persistent state
    auto client = std::static_pointer_cast<ClientContextImpl>(clientIn);
    char buf[4096]{}; // Large stack buffer for performance
    
    // Non-blocking read (pull whatever is available right now)
    ssize_t bytesRead = recv(client->fd, buf, sizeof(buf), 0);

    if(bytesRead < 0)
    {
        // If the socket is non-blocking, EAGAIN means "no more data for now"
        if(errno == EAGAIN || errno == EWOULDBLOCK) 
            return true;
        
        OnError(__FNAME__, __LINE__, "recv error: " + std::string(strerror(errno)));
        return false;
    }
    else if(bytesRead == 0)
    {
        return false; // Connection closed by peer (graceful shutdown)
    }

    size_t start = 0;
    for(size_t i = 0; i < static_cast<size_t>(bytesRead); ++i)
    {
        if(buf[i] == '\n')
        {
            // Calculate length, adjusting for potential \r
            size_t dataLen = i - start;
            if(dataLen > 0 && buf[i - 1] == '\r')
                dataLen--;

            // Update with the current chunk
            EVP_DigestUpdate(client->hashCtx, buf + start, dataLen);

            // Finalize this hash and reset context for the next message in the stream
            unsigned char hash[EVP_MAX_MD_SIZE]{0};
            unsigned int len{0};
            EVP_DigestFinal_ex(client->hashCtx, hash, &len);
            EVP_DigestInit_ex(client->hashCtx, EVP_sha256(), nullptr);

            // Append hash to outputBuffer
            AppendHex(client->outputBuffer, hash, len); 

            // Signal the base class to add EPOLLOUT
            client->wantsWrite = true;

            if(mVerbose)
                OnInfo(__FNAME__, __LINE__, "Generated hash for connection " + std::to_string(client->connectionId));

            start = i + 1;
        }
    }

    // Process any "leftover" data that doesn't end in a newline yet.
    if(start < static_cast<size_t>(bytesRead))
    {
        EVP_DigestUpdate(client->hashCtx, buf + start, bytesRead - start);
    }

    return true;
}

inline bool HashServer::OnWrite(std::shared_ptr<ClientContext>& clientIn)
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

    // Try to send as much as the kernel will take.
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

    // Do we need to stay in EPOLLOUT mode?
    if(client->outputBuffer.empty())
        client->wantsWrite = false; // Buffer empty, stop asking for EPOLLOUT
    else
        client->wantsWrite = true;  // Still data left, keep EPOLLOUT active
    
    return true;
}

// Appends hex data followed by a newline directly to a buffer
void HashServer::AppendHex(std::string& dest, const void* hash, int len) const
{
    size_t oldSize = dest.size();
    dest.resize(oldSize + (len * 2) + 1);
    gen::ToHex(dest.data() + oldSize, hash, len);
    dest.back() = '\n';
}


#endif // __HASH_SERVER_HPP__