#pragma once

#include <condition_variable>
#include <exception>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>

// Thread-safe Channel
template <typename T>
class Channel {
 public:
  struct RecvItem {
    std::optional<T> value;
    bool closed;
  };

  Channel() = default;
  ~Channel() = default;

  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;

  Channel(Channel &&) = delete;
  Channel &operator=(Channel &&) = delete;

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    cv_.notify_all();
  }

  void send(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(value));
    cv_.notify_one();
  }

  RecvItem recv() {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this]() { return !queue_.empty() || closed_; });

    if (queue_.empty()) {
      return {std::nullopt, closed_};
    }

    T value = std::move(queue_.front());
    queue_.pop();

    return {std::move(value), closed_};
  }

 private:
  std::queue<T> queue_;
  std::condition_variable cv_;
  std::mutex mutex_;
  bool closed_ = false;
};

// Helper function to run tasks in another thread

class ThreadExecutor {
 public:
  using Task = std::function<void()>;

  ThreadExecutor() {
    thread_ = std::thread([this]() { run_thread(); });
    worker_thread_id_ = thread_.get_id();
  };
  ~ThreadExecutor() {
    channel_.close();
    thread_.join();
  }
  ThreadExecutor(const ThreadExecutor &) = delete;
  ThreadExecutor &operator=(const ThreadExecutor &) = delete;

  void add_task(Task task) {
    // if same thread, run in current thread instantly, to avoid deadlock
    if (std::this_thread::get_id() == worker_thread_id_) {
      task();
      return;
    }

    channel_.send(std::move(task));
  }

 private:
  void run_thread() {
    while (true) {
      auto [value, closed] = channel_.recv();
      if (closed) {
        break;
      }

      try {
        if (value.has_value()) {
          value.value()();
        }

      } catch (std::exception &e) {
        std::cerr << "ThreadExecutor: failed to run task: " << e.what()
                  << std::endl;
      }
    }
  }

 private:
  std::thread thread_;
  std::thread::id worker_thread_id_;
  Channel<Task> channel_;
};