/**
 *  ===========================================================================
 * /                             ThreadManager                                /
 * ===========================================================================
 *           -- A class to manage creation and managing of threads --
 * 
 * > ThreadManager provides the API to create and manage threads in a unified
 *   manner
 * 
 * > The class has the following public methods:-
 *   (+) std::thread::id spawn_thread(<function>[, <function args...>])
 *              - Allows immediate creation of a thread with the provided
 *                function and its arguments. The function must however have
 *                two std::stop_token arguments as its first and second
 *                parameters, one for a local stop token, one for a global
 *                token.
 *   (+) void reserve(uint32_t limit) - Reserve space for at least `limit` 
 *                                      number of threads
 *   (+) size_t capacity() - Returns the limit (if set, else undefined)
 * 
 *   (+) bool request_stop_all() - Sends a stop request via the global stop
 *                                 token
 *   (+) bool stop_requested_all() - Checks if a global stop was requested
 * 
 *   (+) bool request_stop(const std::threads::id& tid) (throws for tid out of 
 *                                                       bounds)
 *              - Sends a stop request via the local stop tied to the thread
 *                tied to `tid`
 *   (+) bool stop_requested(const std::threads::id& tid) (throws for tid out 
 *                                                         of bounds)
 *              - Checks if a local stop was requested to the thread tied to
 *                `tid`
 * 
 *   (+) void join() - Blocks and attempts to join all the managed threads. This
 *                     call also blocks the creation of new threads, hence a
 *                     concurrent call to spawn_thread() might cause a livelock.
 *                     Clears all dead threads from the manager
 *   (+) bool all_running() - Checks whether all created threads are running.
 *                            Guarantees wait-free response
 *   (+) bool any_running() - Checks whether any created threads are running.
 *                            Guarantees wait-free response
 *   (+) size_t total_threads() - Returns the total number of threads in the
 *                                manager
 *   (+) size_t alive_threads() - Returns the count of threads still running (
 *                                or in the process of shutting down)
 */

#pragma once


#include <atomic>
#include <sstream>
#include <stop_token>
#include <thread>
#include <unordered_map>

#include <cstdint>

/// @brief Allows creation and management of threads
class ThreadManager
{
public:
  ThreadManager(const ThreadManager&) = delete;
  ThreadManager& operator= (const ThreadManager&) = delete;
  ThreadManager(ThreadManager&&) = delete;
  ThreadManager& operator= (ThreadManager&&) = delete;

  ThreadManager() = default;

  // Immediately creates a new thread with the given function and arguments
  template <typename Callable, typename... Args>
  std::thread::id spawn_thread(Callable&& worker, Args&&... args)
  {
    thread_counter_.fetch_add(1, std::memory_order::seq_cst);
    std::lock_guard lock(threads_mtx_);
    std::thread::id tid;

    try
    {
      std::jthread thread(
        [
          this, 
          worker = std::forward<Callable>(worker), 
          ... args = std::forward<Args>(args)
        ]
        (std::stop_token local_stoken) mutable {
          worker(
            local_stoken, 
            global_stop_source_.get_token(), 
            args...
          );
          thread_counter_.fetch_sub(1, std::memory_order::seq_cst);
      });
      tid = thread.get_id();
      threads_[tid] = std::move(thread);
    }
    catch (...)
    {
      thread_counter_.fetch_sub(1, std::memory_order::seq_cst);
      throw;
    }

    return tid;
  }

  // Reserves space for at least `limit` threads
  void reserve(uint32_t limit)
  {
    threads_.max_load_factor(1.0F);
    threads_.rehash(limit);
  }

  // Returns capacity after reserving space
  size_t capacity()
  {
    return threads_.bucket_count();
  }
  
  // Sends global stop request
  bool request_stop_all() noexcept
  {
    return global_stop_source_.request_stop();
  }

  // Checks if global stop request was sent
  bool stop_requested_all() const noexcept
  {
    return global_stop_source_.stop_requested();
  }

  // Sends stop request to thread `tid` 
  bool request_stop(const std::thread::id& tid)
  {
    std::lock_guard lock(threads_mtx_);
    if (!threads_.contains(tid))
    {
      std::ostringstream oss;
      oss << tid;
      throw std::invalid_argument(
        "Thread with id: " + oss.str() + " does not exist"
      );
    }
    return threads_[tid].request_stop();
  }

  // Checks if thread `id` was requested to stop
  bool stop_requested(const std::thread::id& tid)
  {
    std::lock_guard lock(threads_mtx_);
    if (!threads_.contains(tid))
    {
      std::ostringstream oss;
      oss << tid;
      throw std::invalid_argument(
        "Thread with id: " + oss.str() + " does not exist"
      );
    }    
    return threads_.at(tid).get_stop_token().stop_requested();
  }

  // Blocks and attempts to join all the threads, clears dead threads
  void join()
  {
    std::lock_guard lock(threads_mtx_);
    for (auto& [_, thread] : threads_)
    {
      thread.join();
    }
    threads_.clear();
  }

  // Checks if all threads are running
  bool all_running() const noexcept
  {
    return (thread_counter_.load(std::memory_order::acquire) >= threads_.size()) && 
           (threads_.size() != 0);
  }
  // Checks if any thread is running
  bool any_running() const noexcept
  {
    return thread_counter_.load(std::memory_order::acquire) > 0;
  }

  // Returns the total number of threads
  size_t total_threads() const noexcept
  {
    return threads_.size();
  }

  // Returns count of threads still running
  size_t alive_threads() const noexcept
  {
    return thread_counter_.load(std::memory_order::acquire);
  }

private:
  std::atomic<uint32_t> thread_counter_{0};
  std::stop_source global_stop_source_;
  std::unordered_map<std::thread::id, std::jthread, std::hash<std::thread::id>> threads_;
  std::mutex threads_mtx_;
};
