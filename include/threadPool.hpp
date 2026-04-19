//
// threadPool.hpp
//
#ifndef __THREADPOOL_HPP__
#define __THREADPOOL_HPP__

#include <thread>               // std::thread
#include <mutex>                // std::mutex
#include <condition_variable>   // std::condition_variable
#include <functional>           // std::function
#include <deque>                // std::deque
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
    std::deque<std::function<void()>> mReqList;
    bool mStop{false};
    unsigned long mStoppedCount{0};
    unsigned long mReqCount{0};
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
    mThreadCount = threadCount;

    for(int i = 0; i < threadCount; ++i)
    {
        mThreads.emplace_back([this]()
        {
            while(true)
            {
                std::function<void()> func;
                {
                    std::unique_lock<std::mutex> lock(mMutex);

                    // Wait for a task OR a stop signal
                    mCv.wait(lock, [this] { return !mReqList.empty() || mStop; });

                    // Exit the loop if stop is signaled and no tasks are left
                    if(mStop && mReqList.empty())
                        break;

                    // Standard check for spurious wakeups
                    if(mReqList.empty())
                        continue;

                    // Pop the next task
                    func = std::move(mReqList.front());
                    mReqList.pop_front();
                }

                // Execute the task outside of the lock so other threads can work
                func();

                // Task is finished: decrement count and notify any Wait() callers
                {
                    std::unique_lock<std::mutex> lock(mMutex);
                    if(--mReqCount == 0)
                        mCvDone.notify_all();
                }
            }

            // Thread is exiting: update stopped count and notify if it's the last one
            std::unique_lock<std::mutex> lock(mMutex);
            if(++mStoppedCount == mThreads.size())
                mCvDone.notify_all();
        });
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
        mReqList.emplace_back(std::bind(std::forward<FUNC>(func), std::forward<ARGS>(args)...));
    }
    mCv.notify_one();
}

// Wait() will wait until all requests are processed.
inline void ThreadPool::Wait()
{
    std::unique_lock<std::mutex> lock(mMutex);
    
    // If there is no work and we aren't stopping, we can return immediately
    if (mReqCount == 0 && !mStop)
        return;

    // Wait until all requests are processed (mReqCount == 0)
    // OR until all threads have fully exited (mStoppedCount == mThreads.size())
    mCvDone.wait(lock, [this] { 
        return mReqCount == 0 || (mStop && mStoppedCount == mThreads.size()); 
    });
}

// Destroy will terminate all threads.
inline void ThreadPool::Destroy()
{
    // Tell workers to stop
    Stop();
    
    // Wait for them to finish their current task and exit
    JoinThreads();
}

// Stop will signal all threads to exit. 
// It can be called by any thread, including pool threads.
inline void ThreadPool::Stop()
{
    std::unique_lock<std::mutex> lock(mMutex);
    if(mStop)
        return; // Already stopping
        
    mStop = true;
    
    // Wake up all worker threads so they can check the mStop flag and exit
    mCv.notify_all();
}

inline void ThreadPool::JoinThreads()
{
    // Wait for all threads to exit
    for(std::thread& thread : mThreads)
    {
        if(thread.joinable())
            thread.join();
    }
    
    // Cleanup after all threads are stopped
    mThreads.clear();
    mReqList.clear();
    mStop = false;
    mStoppedCount = 0;
    mReqCount = 0;
}

#endif // __THREADPOOL_HPP__
