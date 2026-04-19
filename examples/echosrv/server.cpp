//
// server.cpp
//
#include "echoServer.hpp"
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

    // Create EchoServer
    unsigned int threadsCount = std::thread::hardware_concurrency();
    EchoServer server(threadsCount);
    // server.SetVerbose(true);

    // Starts a helper thread to monitor the exit signal
    std::thread signalObserverThread([&server]() 
    {
        while(gSignalNumber == 0)
            usleep(500000);

        // We got a signal. Stop the server.
        std::cout << __FNAME__<< ":" << __LINE__ << " Got a signal " << gSignalNumber 
                  << " (" << strsignal(gSignalNumber) << "), exiting..." << std::endl;
        server.Stop();
    });

    // Start EchoServer
    if(!server.Start(8080))
    {
        std::cerr << "Failed to start the epoll server." << std::endl;
        return 1;
    }

    // Join the helper thread and exit
    signalObserverThread.join();
    return 0;
}

