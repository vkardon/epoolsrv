//
// threadPool.hpp
//
#ifndef __THREADPOOL_HPP__
#define __THREADPOOL_HPP__

#include <thread>               // std::thread
#include <mutex>                // std::mutex
#include <condition_variable>   // std::condition_variable
#include <functional>           // std::function
#include <list>                 // std::list
#include <assert.h>             // assert()

//
// A class ThreadPool to manage a pool of threads where the task function
// can be different as specified by each request.
//
class ThreadPool
{
public:
    ThreadPool() = default;
    ~ThreadPool() { Destroy(); }

    void Start(int threadCount);

    // Post function to be executed by ThreadPool along with function args
    template<typename FUNC, typename... ARGS>
    void Post(FUNC&& func, ARGS&&... args);

    // Wait() will wait of all pool threads either done processing or stopped.
    // Note: It must not be called by any of pool threads since a thread cannot
    // join itself because of deadlock.
    void Wait();

    // Destroy will terminate all threads.
    // Note: It must not be called by any of pool threads since a thread cannot
    // join itself because of deadlock.
    void Destroy();

    // Stop will force pool threads to exit and hence causes Wait() to return.
    // It can be called by any thread, including pool threads.
    void Stop();

private:
    void JoinThreads();

    int mThreadCount{0};
    std::vector<std::thread> mThreads;
    std::mutex mMutex;
    std::condition_variable mCv;
    std::condition_variable mCvDone;
    std::list<std::function<void()>> mReqList;
    bool mStop{false};
    unsigned long mStoppedCount{0};
    unsigned long mReqCount{0};
    bool mHasMore{false};
};

//
// A class ThreadPoolEx to manage a pool of threads where all threads
// execute the same task function
//
template<typename FUNC>
class ThreadPoolEx : public ThreadPool
{
public:
    ThreadPoolEx(FUNC func) : mFunc(std::move(func)) {};
    ~ThreadPoolEx() = default;

    // Post arguments for a function to be executed by ThreadPool
    template<typename... ARGS>
    void Post(ARGS&&... args) { ThreadPool::Post(mFunc, std::forward<ARGS>(args)...); }

private:
    FUNC mFunc;
};

//
// Class ThreadPool implementation
//
inline void ThreadPool::Start(int threadCount)
{
    assert(mThreads.empty());
    mThreads.resize(threadCount);

    for(auto& thread : mThreads)
    {
        thread = std::thread([&]()
        {
            bool isProcessing = false;

            while(true)
            {
                std::unique_lock<std::mutex> lock(mMutex);

                // If we were processing before, then update request count.
                // Make "Done" notification once all requests are processed
                // to unblock Wait()
                if(isProcessing && --mReqCount == 0 && !mHasMore)
                    mCvDone.notify_one();

                // Wait for a "New Request" notification
                isProcessing = false;
                while(mReqList.empty() && !mStop)
                    mCv.wait(lock);

                // Do we have to stop?
                if(mStop)
                    break;

                if(mReqList.empty())
                    continue; // We shouldn't be here, but just in case

                // Pop the front element
                auto func = std::move(mReqList.front());
                mReqList.pop_front();

                // Release the lock to let other threads go
                lock.unlock();

                // Process the request
                isProcessing = true;
                func();
            }

            // If we are stopping, then update stopped threads count.
            // Make "Done" notification once all treads are stopped
            // to unblock Wait()
            std::unique_lock<std::mutex> lock(mMutex);

            if(++mStoppedCount == mThreads.size())
                mCvDone.notify_one();

        }); // End of thread lambda
    }
}

template<class FUNC, class... ARGS>
inline void ThreadPool::Post(FUNC&& func, ARGS&&... args)
{
    // Add request to the list for a next available thread to pick up
    {
        std::unique_lock<std::mutex> lock(mMutex);
        if(mStop)
            return;
        mReqCount++;
        mHasMore = true;
        mReqList.emplace_back(std::bind(std::forward<FUNC>(func), std::forward<ARGS>(args)...));
    }
    mCv.notify_one();
}

// Wait() will wait of all pool threads either done processing or stopped.
// Note: It must not be called by any of pool threads since a thread cannot
// join itself because of deadlock.
inline void ThreadPool::Wait()
{
    // Indicate that we are not going to add more
    // request and then wait for a "Done" notification
    std::unique_lock<std::mutex> lock(mMutex);
    mHasMore = false;
    if(mReqCount == 0)
        return; // All requests are processed or never started

    // We use loop to handle spurious wakeups
    while(true)
    {
        mCvDone.wait(lock);

        if(mStop)
        {
            if(mStoppedCount == mThreads.size())
            {
                // If are here because of "Stop" action, then
                // all threads must be already stopped.
                assert(mStoppedCount == mThreads.size());

                // Wait for all threads to exit
                JoinThreads();

                // Restart threads
                Start(mThreadCount);
                break;
            }
        }
        else if(mReqCount == 0)
        {
            break;
        }
    }
}

// Destroy will terminate all threads.
// Note: It must not be called by any of pool threads since a thread cannot
// join itself because of deadlock.
inline void ThreadPool::Destroy()
{
    // Stop all threads and wait for them to exit
    Stop();
    JoinThreads();
}

// Stop will force pool threads to exit and hence causes Wait() to return.
// It can be called by any thread, including pool threads.
inline void ThreadPool::Stop()
{
    std::unique_lock<std::mutex> lock(mMutex);
    if(mStop)
        return; // Already stopped or in a process of stopping
    mStop = true;
    lock.unlock();
    mCv.notify_all();
}

inline void ThreadPool::JoinThreads()
{
    // Wait for all threads to exit
    for(std::thread& thread : mThreads)
        thread.join();

    // Cleanup after all threads are stopped
    mThreads.clear();
    mReqList.clear();
    mStop = false;
    mStoppedCount = 0;
    mReqCount = 0;
}

#endif // __THREADPOOL_HPP__
