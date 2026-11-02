/*********************************************************
*
*  Copyright (C) 2014 by Vitaliy Vitsentiy
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*********************************************************/

// OpenArkStudio modifications: retire partial constructor workers and preserve
// callback/future ownership until queue publication succeeds.
#ifndef __ctpl_stl_thread_pool_H__
#define __ctpl_stl_thread_pool_H__

#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// thread pool to run user's functors with signature
//      ret func(int id, other_params)
// where id is the index of the thread that runs the functor
// ret is some return type

namespace ctpl {

namespace detail {
template<typename T>
class queue_type
{
public:
    bool push(T const& value) {
        std::unique_lock<std::mutex> lock(this->mutex);
        this->q.push(value);
        return true;
    }

    // deletes the retrieved element, do not use for non integral types
    bool pop(T& v) {
        std::unique_lock<std::mutex> lock(this->mutex);
        if (this->q.empty())
        {
            return false;
        }
        v = this->q.front();
        this->q.pop();
        return true;
    }

    bool empty() {
        std::unique_lock<std::mutex> lock(this->mutex);
        return this->q.empty();
    }

private:
    std::queue<T> q;
    std::mutex mutex;
};
} // namespace detail

class thread_pool
{

public:
    thread_pool() {
        this->init();
    }

    thread_pool(int nThreads) {
        this->init();
        try
        {
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
            CTPL_CONSTRUCTION_TEST_STAGE("constructor resize");
#endif
            this->resize(nThreads);
        }
        catch (...)
        {
            // A failed constructor does not run ~thread_pool(). Retire any
            // workers already created before their joinable threads unwind.
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
            CTPL_CONSTRUCTION_TEST_STAGE("constructor catch");
#endif
            this->stop(true);
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
            CTPL_CONSTRUCTION_TEST_STAGE("constructor cleanup complete");
#endif
            throw;
        }
    }

    // the destructor waits for all the functions in the queue to be finished
    ~thread_pool() {
        this->stop(true);
    }

    // get the number of running threads in the pool
    int size() {
        return static_cast<int>(this->threads.size());
    }

    // number of idle threads
    int n_idle() {
        return this->nWaiting;
    }

    std::thread& get_thread(int i) {
        return *this->threads[i];
    }

    // change the number of threads in the pool
    // should be called from one thread, otherwise be careful to not interleave, also with this->stop()
    // nThreads must be >= 0
    void resize(int nThreads) {
        if (!this->isStop && !this->isDone)
        {
            int oldNThreads = static_cast<int>(this->threads.size());
            if (oldNThreads <= nThreads)
            { // if the number of threads is increased
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
                CTPL_CONSTRUCTION_TEST_STAGE("resize thread storage");
#endif
                this->threads.resize(nThreads);
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
                CTPL_CONSTRUCTION_TEST_STAGE("resize flag storage");
#endif
                this->flags.resize(nThreads);

                for (int i = oldNThreads; i < nThreads; ++i)
                {
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
                    CTPL_CONSTRUCTION_TEST_STAGE("allocate worker flag");
#endif
                    this->flags[i] = std::make_shared<std::atomic<bool>>(false);
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
                    CTPL_CONSTRUCTION_TEST_STAGE("create worker thread");
#endif
                    this->set_thread(i);
                }
            }
            else
            { // the number of threads is decreased
                for (int i = oldNThreads - 1; i >= nThreads; --i)
                {
                    *this->flags[i] = true; // this thread will finish
                    this->threads[i]->detach();
                }
                {
                    // stop the detached threads that were waiting
                    std::unique_lock<std::mutex> lock(this->mutex);
                    this->cv.notify_all();
                }
                this->threads.resize(nThreads); // safe to delete because the threads are detached
                this->flags.resize(
                    nThreads); // safe to delete because the threads have copies of shared_ptr of the flags, not originals
            }
        }
    }

    // empty the queue
    void clear_queue() {
        std::function<void(int id)>* _f;
        while (this->q.pop(_f))
        {
            delete _f; // empty the queue
        }
    }

    // pops a functional wrapper to the original function
    std::function<void(int)> pop() {
        std::function<void(int id)>* _f = nullptr;
        this->q.pop(_f);
        std::unique_ptr<std::function<void(int id)>> func(
            _f); // at return, delete the function even if an exception occurred
        std::function<void(int)> f;
        if (_f)
        {
            f = *_f;
        }
        return f;
    }

    // wait for all computing threads to finish and stop all threads
    // may be called asynchronously to not pause the calling thread while waiting
    // if isWait == true, all the functions in the queue are run, otherwise the queue is cleared without running the functions
    void stop(bool isWait = false) {
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
        CTPL_CONSTRUCTION_TEST_STAGE("stop entered");
#endif
        if (!isWait)
        {
            if (this->isStop)
            {
                return;
            }
            this->isStop = true;
            for (int i = 0, n = this->size(); i < n; ++i)
            {
                if (i < static_cast<int>(this->flags.size()) && this->flags[i])
                {
                    *this->flags[i] = true; // command the threads to stop
                }
            }
            this->clear_queue(); // empty the queue
        }
        else
        {
            if (this->isDone || this->isStop)
            {
                return;
            }
            this->isDone = true; // give the waiting threads a command to finish
        }
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
        CTPL_CONSTRUCTION_TEST_STAGE("stop notify workers");
#endif
        {
            std::unique_lock<std::mutex> lock(this->mutex);
            this->cv.notify_all(); // stop all waiting threads
        }
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
        CTPL_CONSTRUCTION_TEST_STAGE("stop join workers");
#endif
        for (int i = 0; i < static_cast<int>(this->threads.size()); ++i)
        { // wait for the computing threads to finish
            if (this->threads[i] && this->threads[i]->joinable())
            {
                this->threads[i]->join();
            }
        }
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
        CTPL_CONSTRUCTION_TEST_STAGE("stop clear queue");
#endif
        // if there were no threads in the pool but some functors in the queue, the functors are not deleted by the threads
        // therefore delete them here
        this->clear_queue();
        this->threads.clear();
        this->flags.clear();
#ifdef CTPL_CONSTRUCTION_TEST_STAGE
        CTPL_CONSTRUCTION_TEST_STAGE("stop complete");
#endif
    }

    template<typename F, typename... Rest>
    auto push(F&& f, Rest&&... rest) -> std::future<decltype(f(0, rest...))> {
        auto pck = std::make_shared<std::packaged_task<decltype(f(0, rest...))(int)>>(
            std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Rest>(rest)...));
        auto result = pck->get_future();
        auto _f = std::make_unique<std::function<void(int id)>>([pck](int id) { (*pck)(id); });
        std::unique_lock<std::mutex> lock(this->mutex);
        this->q.push(_f.get());
        _f.release(); // publication transfers ownership to the queue/worker
        this->cv.notify_one();
        return result;
    }

    // run the user's function that excepts argument int - id of the running thread. returned value is templatized
    // operator returns std::future, where the user can get the result and rethrow the catched exceptins
    template<typename F>
    auto push(F&& f) -> std::future<decltype(f(0))> {
        auto pck = std::make_shared<std::packaged_task<decltype(f(0))(int)>>(std::forward<F>(f));
        auto result = pck->get_future();
        auto _f = std::make_unique<std::function<void(int id)>>([pck](int id) { (*pck)(id); });
        std::unique_lock<std::mutex> lock(this->mutex);
        this->q.push(_f.get());
        _f.release(); // publication transfers ownership to the queue/worker
        this->cv.notify_one();
        return result;
    }

private:
    // deleted
    thread_pool(const thread_pool&);            // = delete;
    thread_pool(thread_pool&&);                 // = delete;
    thread_pool& operator=(const thread_pool&); // = delete;
    thread_pool& operator=(thread_pool&&);      // = delete;

    void set_thread(int i) {
        std::shared_ptr<std::atomic<bool>> flag(this->flags[i]); // a copy of the shared ptr to the flag
        auto f = [this, i, flag /* a copy of the shared ptr to the flag */]() {
            std::atomic<bool>& _flag = *flag;
            std::function<void(int id)>* _f;
            bool isPop = this->q.pop(_f);
            while (true)
            {
                while (isPop)
                { // if there is anything in the queue
                    std::unique_ptr<std::function<void(int id)>> func(
                        _f); // at return, delete the function even if an exception occurred
                    (*_f)(i);
                    if (_flag)
                    {
                        return; // the thread is wanted to stop, return even if the queue is not empty yet
                    }
                    else
                    {
                        isPop = this->q.pop(_f);
                    }
                }
                // the queue is empty here, wait for the next command
                std::unique_lock<std::mutex> lock(this->mutex);
                ++this->nWaiting;
                this->cv.wait(lock, [this, &_f, &isPop, &_flag]() {
                    isPop = this->q.pop(_f);
                    return isPop || this->isDone || _flag;
                });
                --this->nWaiting;
                if (!isPop)
                {
                    return; // if the queue is empty and this->isDone == true or *flag then return
                }
            }
        };
        this->threads[i].reset(new std::thread(f)); // compiler may not support std::make_unique()
    }

    void init() {
        this->nWaiting = 0;
        this->isStop = false;
        this->isDone = false;
    }

    std::vector<std::unique_ptr<std::thread>> threads;
    std::vector<std::shared_ptr<std::atomic<bool>>> flags;
    detail::queue_type<std::function<void(int id)>*> q;
    std::atomic<bool> isDone;
    std::atomic<bool> isStop;
    std::atomic<int> nWaiting; // how many threads are waiting

    std::mutex mutex;
    std::condition_variable cv;
};

} // namespace ctpl

#endif // __ctpl_stl_thread_pool_H__
