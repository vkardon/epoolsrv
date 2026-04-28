//
// server.cpp
//
#include "epollServer.hpp"
#include "protoServer.hpp"
#include "hello.pb.h"
#include <signal.h>

// Handler for SIGHUP, SIGINT, SIGQUIT and SIGTERM
volatile static sig_atomic_t gSignalNumber{0};

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
    // Writing to an unconnected socket will cause a process to receive a SIGPIPE
    // signal. We don't want to die if this happens, so we ignore SIGPIPE.
    Signal(SIGPIPE, SIG_IGN);

    // Let the kernel know that we want to handle exit signals
    Signal(SIGHUP,  HandlerExitSignal);
    Signal(SIGINT,  HandlerExitSignal);
    Signal(SIGQUIT, HandlerExitSignal);
    Signal(SIGTERM, HandlerExitSignal);

    // Create MyServer
    unsigned int threadsCount = std::thread::hardware_concurrency();
    MyServer server(threadsCount);
//    server.SetVerbose(true);

    // Starts a helper thread to monitor the exit signal
    std::thread signalObserverThread([&server]() 
    {
        while(gSignalNumber == 0)
            usleep(500000);

        // We got a signal. Stop the server.
        std::cout << __FNAME__ << ":" << __LINE__ << " Got a signal " << gSignalNumber 
                  << " (" << strsignal(gSignalNumber) << "), exiting..." << std::endl;
        server.Stop();
    });

    // Configure MyServer to listen on both NET socket and Unix Domain socket
    // if(!server.AddListener(8080) ||
    //    !server.AddListener("\0protoserver_domain_socket.sock", true) ||
    //    !server.Start())
//    if(!server.Start(8080))
    if(!server.Start("\0protoserver_domain_socket.sock"))
    {
        std::cerr << "Failed to start the epoll server." << std::endl;
        return 1;
    }

    // Join the helper thread and exit
    signalObserverThread.join();
    return 0;
}

