//
// client.cpp
//
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <chrono>

const int PORT = 8080;

class EchoClient
{
public:
    EchoClient(const std::string& ip, int port) : mIp(ip), mPort(port), mSock(-1) {}
    ~EchoClient() 
    {
        if(mSock != -1) 
            close(mSock);
    }

    bool Connect() 
    {
        mSock = socket(AF_INET, SOCK_STREAM, 0);
        if(mSock < 0) 
        {
            std::cerr << "Failed to create socket: " << std::strerror(errno) << std::endl;
            return false;
        }

        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(mPort);
        inet_pton(AF_INET, mIp.c_str(), &serverAddr.sin_addr);

        if(connect(mSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) 
        {
            std::cerr << "Connection failed to " 
                      << inet_ntoa(serverAddr.sin_addr) << ":" << ntohs(serverAddr.sin_port) 
                      << ", Error: " << std::strerror(errno) << std::endl;            
                      return false;
        }

        return true;
    }

    bool Call(const std::string& msg, std::string& response) 
    {
        // Newline termination required by EchoServer::OnRead
        std::string request = msg + "\n";
        
        if(send(mSock, request.c_str(), request.length(), 0) < 0) 
        {
            std::cerr << "Failed to send data: " << std::strerror(errno) << std::endl;
            return false;
        }

        char buffer[4096]{0};
        ssize_t bytesRead = recv(mSock, buffer, sizeof(buffer) - 1, 0);

        if(bytesRead < 0)
        {
            // A true error occurred
            std::cerr << "Receive error: " << std::strerror(errno) << std::endl;
            return false;
        } 
        else if(bytesRead == 0)
        {
            // The peer closed the connection gracefully
            std::cout << "Connection closed by peer." << std::endl;
            return false;
        }

        if(buffer[bytesRead - 1] != '\n')
        {
            // The response is not newline-teminated.
            // Cast to int to see the ASCII value, especially if it's a non-printable character
            std::cerr << "Protocol error: Response not newline-terminated. "
                      << "Last byte received: 0x" << std::hex << static_cast<int>(static_cast<unsigned char>(buffer[bytesRead - 1]))
                      << std::dec << " ('" << buffer[bytesRead - 1] << "')" << std::endl;
            return false;
        }

        buffer[bytesRead-1] = '\0'; // Drop the newline
        response = std::string(buffer);
        return true;
    }

private:
    std::string mIp;
    unsigned int mPort{0};
    int mSock{0};
};

void RunTest(int numThreads, int numOfCallsPerThread)
{
    // Create and start multiple threads
    std::vector<std::thread> threads;

    for(int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([i, numOfCallsPerThread]()
        {
            EchoClient client("127.0.0.1", PORT);
            if(!client.Connect()) 
                return;

            for(int j = 0; j < numOfCallsPerThread; j++)
            {
                std::string req = "Thread " + std::to_string(i) + " call " + std::to_string(j);
                std::string resp;

                if(client.Call(req, resp)) 
                {
                    // Silence output in stress test to avoid console lag
                    //std::cout << "[" << i << "] Req: '" << req << "', Resp: '" << resp << "'" << std::endl;
                }
            }
        });
    }

    for(auto& thread : threads)
        thread.join();
}

int main()
{
    // Writing to an unconnected socket will cause a process to receive a SIGPIPE
    // signal. We don't want to die if this happens, so we ignore SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    int numOfThreadsPerRun = 500;
    int numOfCallsPerThread = 500;
    int numOfRuns = 10;

    std::cout << "Running:\n"
            << "  Number of threads          : " << numOfThreadsPerRun << "\n"
            << "  Number of calls per thread : " << numOfCallsPerThread << "\n"
            << "  Number of runs             : " << numOfRuns << std::endl;

    for(int i = 0; i < numOfRuns; i++)
    {
        std::cout << "Run " << i << std::endl;
        RunTest(numOfThreadsPerRun, numOfCallsPerThread);
    }

    std::cout << "Done:\n"
            << "  Number of threads          : " << numOfThreadsPerRun << "\n"
            << "  Number of calls per thread : " << numOfCallsPerThread << "\n"
            << "  Number of runs             : " << numOfRuns << std::endl;

    return 0;
}
