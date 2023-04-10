#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "fmt/core.h"
#include "lumidb/db.hh"
#include "lumidb/query.hh"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"

// Manage internal states of Timers
class TimerManager;

class TimerPlugin {
 public:
  TimerPlugin(lumidb::Database *db);
  ~TimerPlugin();
  int on_load();

 private:
  lumidb::Database *db_;
  std::shared_ptr<TimerManager> manager_;
};

// unit: 1 second
using time_unit_t = int;
using task_id_t = std::string;

struct Task {
  Task(std::function<void()> func, time_unit_t interval, time_unit_t deadline)
      : func(std::move(func)), interval(interval), deadline(deadline) {}

  // the heap is a max heap, so we need to reverse the order
  // the top element has the earliest deadline
  bool operator<(const Task &other) const { return deadline > other.deadline; }

  std::function<void()> func;
  time_unit_t interval;
  time_unit_t deadline;
  bool deleted = false;
};

using TaskPtr = std::shared_ptr<Task>;

// Manage Scheduled Task
class TimedTaskScheduler {
 public:
  using FunctionList = std::vector<std::function<void()>>;

  // return task_id
  void add_task(std::string id, std::function<void()> func,
                time_unit_t interval) {
    if (interval <= 0) {
      throw std::invalid_argument("interval must be positive");
    }

    time_unit_t deadline = now_ + interval;
    auto task = std::make_shared<Task>(std::move(func), interval, deadline);

    tasks_.push(task);
    tasks_map_[id] = task;
  }

  void remove_task(const std::string &task_id) {
    auto it = tasks_map_.find(task_id);
    if (it == tasks_map_.end()) {
      return;
    }

    auto task = it->second;
    task->deleted = true;

    tasks_map_.erase(it);
  }

  // update now and return the tasks that are ready to run
  FunctionList tick(int now) {
    now_ = now;

    FunctionList funcs;
    while (!tasks_.empty()) {
      auto task = tasks_.top();
      if (task->deadline > now_) {
        break;
      }
      tasks_.pop();

      // delayed deletion
      if (task->deleted) {
        continue;
      }

      funcs.push_back(task->func);

      // reschedule
      task->deadline += task->interval;
      tasks_.push(task);
    }

    return funcs;
  }

 private:
  std::unordered_map<task_id_t, TaskPtr> tasks_map_;
  std::priority_queue<TaskPtr> tasks_;
  long now_ = 0;
};

using ClockType = std::chrono::system_clock;

struct TimerDesc {
  std::string id;
  std::string time_string;
  std::string query_string;
};

// Must be thread-safe
class TimerManager {
 public:
  TimerManager(lumidb::Database *db) : db_(db) {
    tick_thread_ = std::thread([this]() {
      while (true) {
        if (!running_) {
          return;
        }

        // tick every 500ms
        auto funcs = tick_();

        // elapsed
        auto now = ClockType::now();
        for (auto &func : funcs) {
          func();
        }
        auto elapsed = ClockType::now() - now;

        auto expected_sleep_time = std::chrono::milliseconds(500);

        // sleep for the remaining time
        if (elapsed < expected_sleep_time) {
          std::this_thread::sleep_for(expected_sleep_time - elapsed);
        }
      }
    });
  }
  ~TimerManager() {
    if (running_) {
      running_ = false;
      tick_thread_.join();
    }
  }

  lumidb::Result<std::string> add_timer(std::string time_string,
                                        std::string query_string) {
    std::lock_guard lock(mutex_);

    auto time_res = parse_time(time_string);
    if (time_res.has_error()) {
      return time_res.unwrap_err();
    }
    auto interval = time_res.unwrap();

    auto query_res = lumidb::parse_query(query_string);
    if (query_res.has_error()) {
      return query_res.unwrap_err();
    }

    auto query = query_res.unwrap();
    auto db = db_;

    auto timer_id = std::to_string(timer_id_gen_.next_id());

    auto timer_desc = TimerDesc{
        .id = timer_id,
        .time_string = time_string,
        .query_string = query_string,
    };

    scheduler_.add_task(
        timer_id,
        [this, query, timer_desc, db]() {
          db->logging(lumidb::Logger::INFO,
                      fmt::format("[timer plugin]: executing timer id={}, "
                                  "query='{}', interval={}",
                                  timer_desc.id, timer_desc.query_string,
                                  timer_desc.time_string));

          auto res = db->execute(query).get();
          if (res.has_error()) {
            db->report_error({
                .source = "timer-plugin",
                .name = "timed-task",
                .error = res.unwrap_err(),
            });
            return;
          }

          auto result = res.unwrap();
          db->logging(lumidb::Logger::NORMAL, fmt::format("{}", *result));
        },
        interval);

    timers_[timer_id] = timer_desc;

    return timer_id;
  }

  lumidb::Result<bool> remove_timer(const std::string &timer_id) {
    std::lock_guard lock(mutex_);

    if (!timers_.erase(timer_id)) {
      return lumidb::Error("timer not found, id={}", timer_id);
    }

    scheduler_.remove_task(timer_id);

    return true;
  }

  std::vector<TimerDesc> list_timer_descs() {
    std::lock_guard lock(mutex_);

    std::vector<TimerDesc> descs;
    for (auto &[id, desc] : timers_) {
      descs.push_back(desc);
    }

    return descs;
  }

 private:
  TimedTaskScheduler::FunctionList tick_() {
    std::lock_guard lock(mutex_);
    return scheduler_.tick(now_seconds());
  }

  long now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(ClockType::now() -
                                                            start_time_)
        .count();
  }

 private:
  lumidb::Result<time_unit_t> parse_time(const std::string &time_string) {
    // parse string like "50s"
    if (time_string.size() < 2) {
      return lumidb::Error("invalid time string");
    }

    auto unit = time_string.back();
    auto time = time_string.substr(0, time_string.size() - 1);

    int parsed_time = 0;
    try {
      parsed_time = std::stoi(time);
    } catch (const std::invalid_argument &e) {
      return lumidb::Error("invalid time string");
    }

    switch (unit) {
      case 's':
        return parsed_time;
      default:
        return lumidb::Error("invalid time string, only support 's'");
    }

    return parsed_time;
  }

 private:
  std::mutex mutex_;
  std::atomic<bool> running_ = true;

  lumidb::Database *db_;
  TimedTaskScheduler scheduler_;
  std::unordered_map<std::string, TimerDesc> timers_;
  lumidb::IdGenerator timer_id_gen_;
  std::thread tick_thread_;

  ClockType::time_point start_time_ = ClockType::now();
};