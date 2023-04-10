#include "lumidb/db.hh"

#include <atomic>
#include <cstdint>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fmt/core.h"
#include "lumidb/executor.hh"
#include "lumidb/function.hh"
#include "lumidb/plugin.hh"
#include "lumidb/table.hh"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"

using namespace std;
using namespace lumidb;

class StdLogger : public Logger {
 public:
  virtual void log(Logger::LogLevel, const std::string &msg) override {
    cout << msg << endl;
  }
};

// Database in memory
class MemoryDatabase : public lumidb::Database {
 public:
  MemoryDatabase() = default;

  // table related methods
  virtual Result<TablePtr> create_table(
      const CreateTableParams &params) override {
    std::lock_guard lock(mutex_);
    auto [_, ok] = tables_.insert({params.table->name(), params.table});
    if (!ok) {
      return Error("table already exists: {}", params.table->name());
    }

    ++version_;
    return params.table;
  }
  virtual Result<bool> drop_table(const std::string &name) override {
    std::lock_guard lock(mutex_);
    if (tables_.erase(name)) {
      ++version_;
    }
    return true;
  }
  virtual Result<TablePtr> get_table(const std::string &name) const override {
    std::lock_guard lock(mutex_);
    auto it = tables_.find(name);
    if (it == tables_.end()) {
      return Error("table not found: {}", name);
    }
    return it->second;
  }
  virtual Result<TablePtrList> list_tables() const override {
    std::lock_guard lock(mutex_);
    TablePtrList tables;
    for (auto &it : tables_) {
      tables.push_back(it.second);
    }
    return tables;
  }

  // plugin related methods
  virtual Result<PluginPtr> load_plugin(
      const LoadPluginParams &params) override {
    // we should lock here since plugin may call db methods
    auto id = std::to_string(plugin_id_gen_.next_id());
    auto plugin = Plugin::load_plugin(InternalLoadPluginParams{
        .db = this,
        .id = id,
        .path = params.path,
    });
    if (plugin.has_error()) {
      return plugin.unwrap_err();
    }

    std::lock_guard lock(mutex_);
    plugins_[id] = plugin.unwrap();

    ++version_;
    return plugin.unwrap();
  }

  virtual Result<bool> unload_plugin(string id) override {
    std::lock_guard lock(mutex_);
    if (plugins_.erase(id)) {
      ++version_;
    };
    return true;
  }
  virtual Result<PluginPtr> get_plugin(string id) const override {
    std::lock_guard lock(mutex_);
    auto it = plugins_.find(id);
    if (it == plugins_.end()) {
      return Error("plugin not found: {}", id);
    }
    return it->second;
  }
  virtual Result<PluginPtrList> list_plugins() const override {
    std::lock_guard lock(mutex_);
    PluginPtrList plugins;
    for (auto &it : plugins_) {
      plugins.push_back(it.second);
    }
    return plugins;
  }

  // function related methods
  virtual Result<FunctionPtr> register_function(
      const RegisterFunctionParams &params) override {
    std::lock_guard lock(mutex_);
    auto res = _register_function(params);

    ++version_;
    return res;
  }

  virtual Result<bool> register_function_list(
      const std::vector<RegisterFunctionParams> &params_list) override {
    std::lock_guard lock(mutex_);
    for (auto &params : params_list) {
      auto func = _register_function(params);
      if (func.has_error()) {
        return func.unwrap_err();
      }
    }

    ++version_;
    return true;
  }

  virtual Result<bool> unregister_function(const std::string &name) override {
    std::lock_guard lock(mutex_);
    functions_.erase(name);

    ++version_;
    return true;
  }

  virtual Result<bool> unregister_function_list(
      const std::vector<std::string> &name) override {
    std::lock_guard lock(mutex_);
    for (auto &name : name) {
      functions_.erase(name);
    }

    ++version_;
    return true;
  };
  virtual Result<FunctionPtr> get_function(
      const std::string &name) const override {
    std::lock_guard lock(mutex_);
    return _get_function(name);
  }
  virtual Result<FunctionPtrList> list_functions() const override {
    std::lock_guard lock(mutex_);
    FunctionPtrList functions;
    for (auto &it : functions_) {
      functions.push_back(it.second);
    }
    return functions;
  }

  virtual std::future<Result<TablePtr>> execute(const Query &query) override {
    // std::function needs copyable, but promise is only moveable, so we need to
    // wrap it in a shared_ptr
    // It may have performance issue, but it's ok for now
    auto promise = std::make_shared<std::promise<Result<TablePtr>>>();
    auto future = promise->get_future();

    executor_.add_task([this, query, promise = std::move(promise)]() mutable {
      auto res = _execute(query);
      promise->set_value(res);
    });

    return future;
  }

  // execute query, return result as a table
  Result<TablePtr> _execute(const Query &query) {
    // resolve function and its arguments
    std::vector<FunctionPtr> funcs;
    std::vector<ValueList> args_list;

    funcs.reserve(query.functions.size());
    args_list.reserve(query.functions.size());

    // lock here
    {
      std::lock_guard lock(mutex_);
      for (auto &func : query.functions) {
        auto func_ptr_res = this->_get_function(func.name);
        if (func_ptr_res.has_error()) {
          return func_ptr_res.unwrap_err().add_message("failed to resolve");
        }
        auto func_ptr = func_ptr_res.unwrap();

        // check arguments
        auto check_res = func_ptr->signature().check(func.arguments);
        if (check_res.has_error()) {
          return check_res.unwrap_err().add_message(
              "function {} typecheck failed", func_ptr->name());
        }

        funcs.push_back(func_ptr);
        args_list.push_back(func.arguments);
      }

      // check we have at least one function
      if (funcs.empty()) {
        return Error("no function to execute");
      }
    }

    // unlock here

    FunctionPtr root_func = funcs[0];

    if (!root_func->can_root()) {
      return Error("root function {} is not allowed to be root",
                   root_func->name());
    }

    // check leaf function
    for (size_t i = 1; i < funcs.size(); ++i) {
      auto &func = funcs[i];
      if (!func->can_leaf()) {
        return Error("leaf function {} is not allowed to be leaf",
                     func->name());
      }
    }

    // execute functions
    RootFunctionExecuteContext root_exec_ctx{
        .db = this,
        .args = args_list[0],
        .user_data = {},
    };
    RootFunctionFinalizeContext root_final_ctx{
        .db = this,
        .args = args_list[0],
        .user_data = {},
        .result = {},
    };
    LeafFunctionExecuteContext leaf_exec_ctx{
        .db = this,
        .args = {},
        .user_data = {},
        .root_func = root_func,
    };

    // execute root function first

    auto res = root_func->execute_root(root_exec_ctx);
    if (res.has_error()) {
      return res.unwrap_err().add_message("failed to execute: {}",
                                          root_func->name());
    }

    // set root user data
    leaf_exec_ctx.user_data = root_exec_ctx.user_data;

    // execute leaf functions
    for (size_t i = 1; i < funcs.size(); ++i) {
      auto &func = funcs[i];
      auto &args = args_list[i];

      leaf_exec_ctx.args = args;

      res = func->execute_leaf(leaf_exec_ctx);
      if (res.has_error()) {
        return res.unwrap_err().add_message("failed to execute: {}",
                                            func->name());
      }
    }

    // finalize root function
    root_final_ctx.user_data = leaf_exec_ctx.user_data;
    res = root_func->finalize_root(root_final_ctx);
    if (res.has_error()) {
      return res.unwrap_err().add_message("failed to finalize: {}",
                                          root_func->name());
    }

    if (root_final_ctx.result.has_value()) {
      return root_final_ctx.result.value();
    }

    // we just return empty table
    return std::make_shared<Table>("", TableSchema{});
  }

  // helper function
  virtual void report_error(const ReportErrorParams &params) override {
    logger_->log(Logger::ERROR, fmt::format("{}: {}: {}", params.source,
                                            params.name, params.error.message));
  }

  virtual void logging(Logger::LogLevel level,
                       const std::string &msg) override {
    logger_->log(level, msg);
  }

  virtual void set_logger(LoggerPtr logger) override { logger_ = logger; }

  virtual int64_t version() const override { return version_; }

 private:
  Result<FunctionPtr> _get_function(const std::string &name) const {
    auto it = functions_.find(name);
    if (it == functions_.end()) {
      return Error("function not found: {}", name);
    }
    return it->second;
  }

  Result<FunctionPtr> _register_function(const RegisterFunctionParams &params) {
    auto [it, inserted] = functions_.insert({params.func->name(), params.func});
    if (!inserted) {
      return Error("function already exists: {}", params.func->name());
    }
    return it->second;
  }

 private:
  mutable std::mutex mutex_;
  map<string, TablePtr> tables_;
  map<string, FunctionPtr> functions_;
  map<string, PluginPtr> plugins_;
  IdGenerator plugin_id_gen_;
  std::atomic_int64_t version_ = 0;

  ThreadExecutor executor_;

  // !! data race here, we can't use std::atomic_shared_ptr until c++20
  LoggerPtr logger_ = std::make_shared<StdLogger>();
};

Result<DatabasePtr> lumidb::create_database(
    const CreateDatabaseParams &params) {
  auto db = std::make_shared<MemoryDatabase>();

  // register builtin functions
  auto buildin_funcs = get_builtin_functions();

  vector<RegisterFunctionParams> params_list;
  params_list.reserve(buildin_funcs.size());

  for (auto &func : buildin_funcs) {
    params_list.push_back({.func = func});
  }

  auto res = db->register_function_list(params_list);

  if (res.has_error()) {
    return res.unwrap_err().add_message("failed to register builtin functions");
  }

  return Result<DatabasePtr>(db);
}
