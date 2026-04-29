//
// server.cpp
//
#include "epollServer.hpp"
#include "protoServer.hpp"
#include "hello.pb.h"
#include <signal.h>
#include <atomic>
#include <mutex>
#include <condition_variable>

// Synchronizing primitives
static std::atomic<int> gSignalNumber{0};
static std::mutex gSignalMutex;
static std::condition_variable gSignalCV;

extern "C"
void HandlerExitSignal(int signalNumber)
{
    // Once we are in this handler, block all the signals that trigger this handler
    sigset_t blockSignals;
    sigemptyset(&blockSignals);
    sigaddset(&blockSignals, SIGHUP);
    sigaddset(&blockSignals, SIGINT);
    sigaddset(&blockSignals, SIGQUIT);
    sigaddset(&blockSignals, SIGTERM);
    sigprocmask(SIG_BLOCK, &blockSignals, nullptr);

    const char* msg = "Got a signal\n";
    write(STDOUT_FILENO, msg, strlen(msg));

    gSignalNumber = signalNumber;

    // Wake up the observer thread.
    // Note: notify_all is one of the few thread-safe calls allowed here.
    // Since we use an atomic, the observer will definitely see the change.
    gSignalCV.notify_all();
}

int Signal(int signum, void (*handler)(int))
{
    struct sigaction sa, old_sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // want interrupted system calls to be restarted
    return sigaction(signum, &sa, &old_sa);
}

class MyServer : public gen::ProtoServer
{
public:
    MyServer(size_t threadsCount) : gen::ProtoServer(threadsCount) {}
    MyServer() = delete;
    ~MyServer() override = default;

private:
    bool OnInit() override
    {
        return Bind(&MyServer::OnPing);
    }

    void OnError(const char* fname, int lineNum, const std::string& err) const override
    {
        std::cerr << fname << ":" << lineNum << " " << err << std::endl;
    }

    void OnInfo(const char* fname, int lineNum, const std::string& info) const override
    {
        std::cout << fname << ":" << lineNum << " " << info << std::endl;
    }

    void OnPing(const Context& ctx,
                const test::PingRequest& req,
                test::PingResponse& resp)
    {
    //    std::cout << __func__
    //              << ": sessionId='" << ctx.GetMetadata("sessionId") << "'"
    //              << ", requestId='" << ctx.GetMetadata("reportId") << "'"
    //              << ", req=" << req.from() << std::endl;

        resp.set_msg("Pong");
    }
};

int main()
{
    // Let the kernel know that we want to handle exit signals
    Signal(SIGHUP,  HandlerExitSignal);
    Signal(SIGINT,  HandlerExitSignal);
    Signal(SIGQUIT, HandlerExitSignal);
    Signal(SIGTERM, HandlerExitSignal);

    // Create MyServer
    unsigned int threadsCount = std::thread::hardware_concurrency();
    MyServer server(threadsCount);
//    server.SetVerbose(true);

 //    if(!server.Start(8080))
    if(!server.Start("\0protoserver_domain_socket.sock"))
    {
        std::cerr << "Failed to start the epoll server." << std::endl;
        return 1;
    }

    // Main thread waits for an exiting signal
    {
        std::unique_lock<std::mutex> lock(gSignalMutex);
        gSignalCV.wait(lock, []{ return gSignalNumber != 0; });
    }

    std::cout << "Exiting on signal " << gSignalNumber << " (" << strsignal(gSignalNumber) << ")..." << std::endl;
    server.Stop();
    return 0;
}

