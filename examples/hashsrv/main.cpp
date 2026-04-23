//
// main.cpp
//
#include "hashServer.hpp"
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
    // Block signals to prevent nested handler calls
    sigset_t blockSignals;
    sigemptyset(&blockSignals);
    sigaddset(&blockSignals, SIGHUP);
    sigaddset(&blockSignals, SIGINT);
    sigaddset(&blockSignals, SIGQUIT);
    sigaddset(&blockSignals, SIGTERM);
    sigprocmask(SIG_BLOCK, &blockSignals, nullptr);

    const char* msg = "Got a signal\n";
    write(STDOUT_FILENO, msg, strlen(msg));

    // Update state
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

    // Create HashServer
    unsigned int threadsCount = std::thread::hardware_concurrency();
    HashServer server(threadsCount);
    // server.SetVerbose(true);

    // Starts the observer thread to monitor the exit signal
    std::thread signalObserverThread([&server]() 
    {
        // Wait until gSignalNumber is no longer 0
        std::unique_lock<std::mutex> lock(gSignalMutex);
        gSignalCV.wait(lock, []{ return gSignalNumber != 0; });

        if(gSignalNumber > 0)   
        {
            std::cout << __FILE__ << ":" << __LINE__ << " Got a signal " << gSignalNumber 
                      << " (" << strsignal(gSignalNumber) << "), exiting..." << std::endl;
            server.Stop();
        }
    });

    // Start HashServer
    if(!server.Start(8080))
    {
        std::cerr << "Failed to start the epoll server." << std::endl;
        gSignalNumber = -1;  
        gSignalCV.notify_all(); // Wake observer thread to join and exit    
    }

    // Join the observer thread and exit
    signalObserverThread.join();
    return 0;
}

