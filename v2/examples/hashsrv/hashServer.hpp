#ifndef __HASH_SERVER_HPP__
#define __HASH_SERVER_HPP__

#include "epollServer.hpp"
#include <openssl/evp.h>

class HashServer : public gen::EpollServer
{
public:
    HashServer(size_t threadsCount) : gen::EpollServer(threadsCount) {}
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

        EVP_MD_CTX* hashCtx{nullptr};
    };

    std::shared_ptr<gen::EpollServer::ClientContext> MakeClientContext() override { return std::make_shared<ClientContextImpl>(); }
    
    // Triggered when new data is available in client->inboundBuffer
    bool OnDataReceived(std::shared_ptr<ClientContext>& clientIn) override;
    
    // Helper to finalize hash, reset context, and queue the response
    bool FinishAndSendHash(std::shared_ptr<ClientContextImpl>& client);
};

inline bool HashServer::OnDataReceived(std::shared_ptr<ClientContext>& clientIn)
{
    auto client = std::static_pointer_cast<ClientContextImpl>(clientIn);
    size_t processedIdx = 0;

    // Scan the inbound buffer provided by the base class
    for(size_t i = 0; i < client->inboundBuffer.size(); ++i)
    {
        if(client->inboundBuffer[i] == '\n')
        {
            size_t dataLen = i - processedIdx;
            
            // Handle CRLF (Windows style line endings)
            if(dataLen > 0 && client->inboundBuffer[i - 1] == '\r')
                dataLen--;

            // Process the line content
            if(dataLen > 0)
                EVP_DigestUpdate(client->hashCtx, &client->inboundBuffer[processedIdx], dataLen);
            
            // Generate response and reset for the next line
            if(!FinishAndSendHash(client))
                return false;

            processedIdx = i + 1;
        }
    }

    // Clean up processed data so the buffer only contains partial lines
    if(processedIdx > 0)
        client->inboundBuffer.erase(client->inboundBuffer.begin(), client->inboundBuffer.begin() + processedIdx);

    return true;
}

inline bool HashServer::FinishAndSendHash(std::shared_ptr<ClientContextImpl>& client)
{
    unsigned char hash[EVP_MAX_MD_SIZE]{0};
    unsigned int len{0};

    if(EVP_DigestFinal_ex(client->hashCtx, hash, &len) != 1)
        return false;

    // Reset context for subsequent data on the same connection
    EVP_DigestInit_ex(client->hashCtx, EVP_sha256(), nullptr);

    char hex[EVP_MAX_MD_SIZE * 2 + 2]{0}; 
    gen::ToHex(hex, hash, len);
    hex[len * 2] = '\n';
    
    // Hand data to base class for background flushing
    if(!client->Send(hex, (len * 2) + 1))
    {
        OnError(__FNAME__, __LINE__, "Outbound buffer full for ID " + std::to_string(client->connectionId));
        return false;
    }

    return true;
}

#endif // __HASH_SERVER_HPP__