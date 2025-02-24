#pragma once

// Figure out cpp standard version macros
// define manually _CPLUPLUS_CURRENT_VERSION_FOR_POORLY in case of issues (main culprit: MSVC)
#ifndef _CPLUPLUS_CURRENT_VERSION_FOR_POORLY
#   ifdef _MSVC_LANG
#       define _CPLUPLUS_CURRENT_VERSION_FOR_POORLY _MSVC_LANG
#   else
#       ifdef __cplusplus
#           define _CPLUPLUS_CURRENT_VERSION_FOR_POORLY __cplusplus    
#       endif
#   endif
#endif

// Figure out includes for pre- and post- cpp20 versions
#if _CPLUPLUS_CURRENT_VERSION_FOR_POORLY < 202002L
// pre- cpp20 includes
#include <array>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
//
#else
// cpp20+ includes
#include <array>
#include <atomic>
#include <future>
#include <thread>
#include <type_traits>
//
#endif

/*

*/
namespace poorly
{

#if _CPLUPLUS_CURRENT_VERSION_FOR_POORLY < 202002L
    // pre- cpp20 -> use mutex + condvars in traditional way
    namespace // unnamed, not concearning user
    {
        template<typename T>
        struct WaitForOneFutureResultBuffer
        {
            std::exception_ptr m_ex;
            std::condition_variable m_cond;
            std::mutex m_mtx;
            T m_result {};
            bool m_done = false;
        };

        template<>
        struct WaitForOneFutureResultBuffer<void>
        {
            std::exception_ptr m_ex;
            std::condition_variable m_cond;
            std::mutex m_mtx;
            bool m_done = false;
        };

        template<typename T, typename ST>
        inline void startWaitingThread(std::future<T>& futureBuff, std::shared_ptr<WaitForOneFutureResultBuffer<T>>& sharedBuffer, ST&& promiseValueSetter)
        {
            using future_t   = std::future<T>;
            using promise_t  = std::promise<T>;
            using buffer_ptr = std::shared_ptr<WaitForOneFutureResultBuffer<T>>;

            promise_t  providerPromise = {};
            future_t   consumerFuture  = providerPromise.get_future();
            
            std::thread waitingThread = std::thread
            (
                +[](promise_t p, buffer_ptr bufferPtr, ST setPromiseValue)
                {   // Assuming buffer pointer is non-null and is not reseted anywhere
                    try
                    {   
                        auto& buffer = *bufferPtr.get();
            
                        std::unique_lock<std::mutex> lock(buffer.m_mtx);
                        buffer.m_cond.wait(lock, [&]() { return buffer.m_done; });
            
                        //buffer.m_ex ? p.set_exception(buffer.m_ex) : p.set_value(std::move(buffer.m_result));
                        buffer.m_ex ? p.set_exception(buffer.m_ex) : setPromiseValue(p, buffer);
                    }
                    catch(std::exception& ex)
                    {
                        p.set_exception(std::make_exception_ptr(ex));
                    }
                },
                std::move(providerPromise), sharedBuffer, std::move(promiseValueSetter)
            );
            
            waitingThread.detach();
            
            futureBuff = std::move(consumerFuture);
        }

        template<typename T, typename WT>
        inline void addFutureProducer(std::future<T>&& fut, std::shared_ptr<WaitForOneFutureResultBuffer<T>>& sharedBuffer, WT&& futureBufferWriter)
        {
            using future_t     = std::future<T>;
            using buffer_t     = WaitForOneFutureResultBuffer<T>;
            using buffer_ptr   = std::shared_ptr<buffer_t>;
            using lock_guard_t = typename std::lock_guard<std::mutex>;

            auto taskFunc = +[](future_t&& fut, buffer_ptr bufferPtr, WT writeToBufferFromFuture)
            {   // Assuming bufferPtr never null or reseted
                auto& buffer = *bufferPtr.get();
                try
                {
                    // await result
                    fut.wait();
                    
                    {   // Locked scope
                        lock_guard_t lock(buffer.m_mtx);
                        if(!buffer.m_done)
                        {
                            buffer.m_done = true;
                            writeToBufferFromFuture(buffer, fut);
                        }
                    }
                    buffer.m_cond.notify_one();
                }
                catch(std::exception& ex)
                {
                    {   // Locked scope
                        lock_guard_t lock(buffer.m_mtx);
                        if(!buffer.m_done)
                        {
                            buffer.m_done = true;
                            buffer.m_ex = std::make_exception_ptr(ex);
                        }
                    }
                    buffer.m_cond.notify_one();
                }
            };

            std::thread taskThread{ taskFunc, std::move(fut), sharedBuffer, std::move(futureBufferWriter)};
            taskThread.detach();
        }
    }
        
    template<typename T>
    struct awaitAccumulatorFor
    {
        awaitAccumulatorFor(std::future<T>& futureBuff)
        { 
            startWaitingThread(futureBuff, m_buff,
            +[](std::promise<T>& prom, WaitForOneFutureResultBuffer<T>& buffr)
            {
                prom.set_value(std::move(buffr.m_result));
            });
        }

        void add(std::future<T>&& fut)
        {
            addFutureProducer(std::move(fut), m_buff,
            +[](WaitForOneFutureResultBuffer<T>& buffr, std::future<T>& awaitedFuture)
            {
                buffr.m_result = std::move(awaitedFuture.get());
            });
        }

        bool isDone() const
        {
            bool ret = false;
            auto* buffPtr = m_buff.get();
            if(buffPtr)
            {
                std::lock_guard<std::mutex> lock(buffPtr->m_mtx);
                ret = buffPtr->m_done;
            }

            return ret;
        }
    private:
        std::shared_ptr<WaitForOneFutureResultBuffer<T>> m_buff = std::make_shared<WaitForOneFutureResultBuffer<T>>();
    };
    
    template<>
    struct awaitAccumulatorFor<void>
    {
        awaitAccumulatorFor(std::future<void>& futureBuff)
        {
            startWaitingThread(futureBuff, m_buff,
            +[](std::promise<void>& prom, WaitForOneFutureResultBuffer<void>& buffr)
            {   buffr; // Silence warnings, set with void has no argument!
                prom.set_value();
            });
        }

        void add(std::future<void>&& fut)
        { 
            addFutureProducer(std::move(fut), m_buff,
            +[](WaitForOneFutureResultBuffer<void>& buffr, std::future<void>& awaitedFuture)
            {   buffr; // Silence warnings
                awaitedFuture.get();
            });
        }

        bool isDone() const
        {
            bool ret = false;
            auto* buffPtr = m_buff.get();
            if(buffPtr)
            {
                std::lock_guard<std::mutex> lock(buffPtr->m_mtx);
                ret = buffPtr->m_done;
            }

            return ret;
        }
    private:
        std::shared_ptr<WaitForOneFutureResultBuffer<void>> m_buff = std::make_shared<WaitForOneFutureResultBuffer<void>>();
    };

#else 
    // cpp20+ -> use atomics with .wait() and .notify()
    namespace // unnamed, not concearning user
    {
        using atomic_underlying_t = char;
        namespace en_status 
        {
            constexpr atomic_underlying_t open   = 0;
            constexpr atomic_underlying_t locked = 1;
            constexpr atomic_underlying_t done   = 2;
        };

        template<typename T>
        struct WaitForOneFutureResultBuffer
        {
            constexpr WaitForOneFutureResultBuffer() : m_ex(), m_result(), m_status(en_status::open)
            {};

            std::exception_ptr m_ex;
            T m_result;
            std::atomic<atomic_underlying_t> m_status;
        };

        template<>
        struct WaitForOneFutureResultBuffer<void>
        {
            inline WaitForOneFutureResultBuffer() : m_ex(), m_status(en_status::open)
            {};

            std::exception_ptr m_ex;
            std::atomic<atomic_underlying_t> m_status;
        };


        template<typename T, typename ST>
        inline void startWaitingThread(std::future<T>& futureBuff, std::shared_ptr<WaitForOneFutureResultBuffer<T>>& sharedBuffer, ST&& promiseValueSetter)
        {
            using future_t = std::future<T>;
            using promise_t = std::promise<T>;
            using buffer_ptr = std::shared_ptr<WaitForOneFutureResultBuffer<T>>;

            promise_t  providerPromise = {};
            future_t   consumerFuture  = providerPromise.get_future();
            
            std::thread waitingThread = std::thread
            (
                +[](promise_t p, buffer_ptr bufferPtr, ST setPromiseValue)
                {
                    try
                    {
                        auto& buffer = *bufferPtr.get();
                        auto& en_status = buffer.m_status;
                        atomic_underlying_t waitUntilNotOpen = en_status::open;

                        // Spinlock for spurious wakeup in between mark
                        while(en_status != en_status::done)
                        {
                            buffer.m_status.wait(waitUntilNotOpen, std::memory_order_acquire);
                        }
                        
            
                        //buffer.m_ex ? p.set_exception(buffer.m_ex) : p.set_value(std::move(buffer.m_result));
                        buffer.m_ex ? p.set_exception(buffer.m_ex) : setPromiseValue(p, buffer);
                    }
                    catch(std::exception& ex)
                    {
                        p.set_exception(std::make_exception_ptr(ex));
                    }
                },
                std::move(providerPromise), sharedBuffer, std::move(promiseValueSetter)
            );
            
            waitingThread.detach();
            
            futureBuff = std::move(consumerFuture);
        }

        template<typename T>
        inline bool _tryLock(std::atomic<T>& lockFlag) noexcept
        {
            atomic_underlying_t expected = en_status::open;
            return lockFlag.compare_exchange_strong(expected, en_status::locked);
        }

        template<typename T>
        inline void markAsDone(std::atomic<T>& lockFlag)
        {
            lockFlag.store(en_status::done, std::memory_order_release);
            lockFlag.notify_all();
        }

        template<typename T, typename WT>
        inline void addFutureProducer(std::future<T>&& fut, std::shared_ptr<WaitForOneFutureResultBuffer<T>>& sharedBuffer, WT&& futureBufferWriter)
        {
            using future_t = std::future<T>;
            using buffer_t = WaitForOneFutureResultBuffer<T>;
            using buffer_ptr = std::shared_ptr<buffer_t>;

            auto taskFunc = +[](future_t fut, buffer_ptr bufferPtr, WT writeToBufferFromFuture)
            {   // Assuming sharedBuffer never null or reseted
                auto& buffer = *bufferPtr.get();
                auto& status = buffer.m_status;
                try
                {
                    // await result
                    fut.wait();

                    bool aquired = _tryLock(status);
                    if(aquired)
                    {
                        writeToBufferFromFuture(buffer, fut);
                        markAsDone(status);
                    }

                }
                catch(std::exception& ex)
                {
                    bool aquired = _tryLock(status);
                    if(aquired)
                    {
                        buffer.m_ex = std::make_exception_ptr(std::move(ex));
                        markAsDone(status);
                    }
                }
            };

            std::thread taskThread{ taskFunc, std::move(fut), sharedBuffer, std::move(futureBufferWriter) };
            taskThread.detach();
        }
    }

    ///*
    template<typename T>
    struct awaitAccumulatorFor
    {
        awaitAccumulatorFor(std::future<T>& futureBuff)
        {
            startWaitingThread(futureBuff, m_buff,
            +[](std::promise<T>& prom, WaitForOneFutureResultBuffer<T>& buffr)
            {
                prom.set_value(std::move(buffr.m_result));
            });
        }

        void add(std::future<T>&& fut)
        {
            addFutureProducer(std::move(fut), m_buff,
            +[](WaitForOneFutureResultBuffer<T>& buffr, std::future<T>& awaitedFuture)
            {   // future is guaranteed be done before this call
                buffr.m_result = std::move(awaitedFuture.get());
            });
        }

        bool isDone() const
        {
            auto* buffPtr = m_buff.get();
            return buffPtr ? buffPtr->m_status == en_status::done : false;
        }

    private:
        std::shared_ptr<WaitForOneFutureResultBuffer<T>> m_buff = std::make_shared<WaitForOneFutureResultBuffer<T>>();
    };
    //*/

    template<>
    struct awaitAccumulatorFor<void>
    {
        awaitAccumulatorFor(std::future<void>& futureBuff)
        {
            startWaitingThread(futureBuff, m_buff,
                +[](std::promise<void>& prom, WaitForOneFutureResultBuffer<void>& buffr)
                {   buffr; // Silence warnings, set with void has no argument!
                    prom.set_value();
                });
        }

        void add(std::future<void>&& fut)
        {
            addFutureProducer(std::move(fut), m_buff, 
                +[](WaitForOneFutureResultBuffer<void>& buffr, std::future<void>& awaitedFuture)
                {   buffr; // Silence warnings
                    awaitedFuture.get();
                });
        }

        bool isDone() const
        {
            auto* buffPtr = m_buff.get();
            return buffPtr ? buffPtr->m_status == en_status::done : false;
        }

    private:
        std::shared_ptr<WaitForOneFutureResultBuffer<void>> m_buff = std::make_shared<WaitForOneFutureResultBuffer<void>>();
    };
#endif
    // Standard-agnostic stuff

    template<typename T>
    inline constexpr awaitAccumulatorFor<T> make_await_accumulator_for(std::future<T>& fut)
    {
        return awaitAccumulatorFor<T>(fut);
    }

    template<typename T>
    inline constexpr std::shared_ptr<awaitAccumulatorFor<T>> make_shared_await_accumulator_for(std::future<T>& fut)
    {
        return std::make_shared<awaitAccumulatorFor<T>>(fut);
    }

    template<typename... Futures>
    typename std::decay<std::common_type_t<Futures...>>::type waitForOneFutureOf(Futures&&... futures)
    {
        using future_t      = typename std::decay<std::common_type_t<Futures...>>::type;

        future_t returnFuture = {};
        auto accumulator = make_await_accumulator_for(returnFuture);
                
        // Make array out of given futures and start producing task threads
        std::array<future_t, sizeof... (futures)> futuresArr = { std::move(futures)... };
        for(auto& f : futuresArr)
        {
            accumulator.add(std::move(f));
        }

        return returnFuture;
    }  
}

#ifndef _CPLUPLUS_CURRENT_VERSION_FOR_POORLY
#undef _CPLUPLUS_CURRENT_VERSION_FOR_POORLY
#endif