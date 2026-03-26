//
// client.cpp
//
#include <iostream>
#include <thread>
#include <vector>
#include <signal.h>
#include <dirent.h>     // readdir
#include "protoClient.hpp"
#include "hello.pb.h"

const int PORT = 8080;
const char* domainSocket = "\0protoserver_domain_socket.sock";

int getCurrentOpenFdCount()
{
    int count = 0;
    DIR *dir = opendir("/proc/self/fd");
    if(dir)
    {
        struct dirent* entry;
        while((entry = readdir(dir)) != nullptr)
        {
            // We count all entries except "." and ".." which are the current and parent directories.
            if(strcmp(entry->d_name, ".") != 0
                    && strcmp(entry->d_name, "..") != 0)
            {
                count++;
            }
        }
        closedir(dir);
    }
    return count;
}

void RunTest(int numThreads, int numOfCallsPerThread)
{
    // Create and start multiple threads
    std::vector<std::thread> threads;

    for(int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([i, numOfCallsPerThread]()
        {
            test::PingRequest req;
            test::PingResponse resp;
            std::string errMsg;
            int timeout = 3000; // ms

            std::map<std::string, std::string> metadata;
            metadata["sessionId"] = "sessionId_1234";
            metadata["reportId"] = "reportId_1234";

            req.set_from("From test application: " + std::to_string(i));

//            gen::ProtoClient protoClient("127.0.0.1", PORT);
            gen::ProtoClient protoClient(domainSocket);

            for(int j = 0; j < numOfCallsPerThread; j++)
            {
//                gen::ProtoClient protoClient("127.0.0.1", PORT);
//                gen::ProtoClient protoClient(domainSocket);

                if(!protoClient.Call(req, resp, metadata, errMsg, timeout))
                {
                    std::cout << "Call() returned ERROR: " << errMsg << std::endl;
                }
                else
                {
                    //std::cout << "Resp[" << j << "]: msg='" << resp.msg() << "'" << std::endl;
                }
            }

//            std::cout << "Resp[" << i << "]: msg='" << resp.msg() << "'" << std::endl;
        });
    }

    for (auto& thread : threads)
        thread.join();
}

int main()
{
    // Writing to an unconnected socket will cause a process to receive a SIGPIPE
    // signal. We don't want to die if this happens, so we ignore SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    int numOfThreadsPerRun = 100;
    int numOfCallsPerThread = 100;
    int numOfRuns = 10;
//    int numOfThreadsPerRun = 10;
//    int numOfCallsPerThread = 10;
//    int numOfRuns = 10;

    std::cout << "Running:\n"
            << "  Number of threads          : " << numOfThreadsPerRun << "\n"
            << "  Number of calls per thread : " << numOfCallsPerThread << "\n"
            << "  Number of runs             : " << numOfRuns << std::endl;

//    std::cout << "Open files: " << getCurrentOpenFdCount() << std::endl;

    for(int i = 0; i < numOfRuns; i++)
    {
        std::cout << "Run " << i << std::endl;
        RunTest(numOfThreadsPerRun, numOfCallsPerThread);
//        std::cout << "Open files: " << getCurrentOpenFdCount() << std::endl;
    }

    std::cout << "Done:\n"
            << "  Number of threads          : " << numOfThreadsPerRun << "\n"
            << "  Number of calls per thread : " << numOfCallsPerThread << "\n"
            << "  Number of runs             : " << numOfRuns << std::endl;

    return 0;
}
