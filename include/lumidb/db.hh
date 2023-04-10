#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "lumidb/query.hh"
#include "lumidb/types.hh"

namespace lumidb {

class Function;
class Plugin;
class Table;

using FunctionPtr = std::shared_ptr<Function>;
using TablePtr = std::shared_ptr<Table>;
using PluginPtr = std::shared_ptr<Plugin>;

using FunctionPtrList = std::vector<FunctionPtr>;
using TablePtrList = std::vector<TablePtr>;
using PluginPtrList = std::vector<PluginPtr>;

template <typename... Args>
TablePtr make_table_ptr(Args &&...args) {
  return std::make_shared<Table>(std::forward<Args...>(args...));
}

template <typename T, typename... Args>
FunctionPtr make_function_ptr(Args &&...args) {
  return std::make_shared<T>(std::forward<Args...>(args...));
}

template <typename T>
FunctionPtr make_function_ptr() {
  return std::make_shared<T>();
}

struct ReportErrorParams {
  std::string source;
  std::string name;
  Error error;
};

struct CreateTableParams {
  TablePtr table;
};

struct LoadPluginParams {
  std::string path;
};

struct RegisterFunctionParams {
  FunctionPtr func;
};

// Database Interface, can be accessed by plugins, functions ...
// The implementation of this interface is in src/lumidb/db.cc
class Database {
 public:
  Database() = default;
  virtual ~Database() = default;

  // metadata
  // every time the database metadata (table, plugins, functions) are modified,
  // the version is increased
  virtual int64_t version() const = 0;

  // table related methods
  virtual Result<TablePtr> create_table(const CreateTableParams &params) = 0;
  virtual Result<bool> drop_table(const std::string &name) = 0;
  virtual Result<TablePtr> get_table(const std::string &name) const = 0;
  virtual Result<TablePtrList> list_tables() const = 0;

  // plugin related methods
  virtual Result<PluginPtr> load_plugin(const LoadPluginParams &params) = 0;
  virtual Result<bool> unload_plugin(plugin_id_t id) = 0;
  virtual Result<PluginPtr> get_plugin(plugin_id_t id) const = 0;
  virtual Result<PluginPtrList> list_plugins() const = 0;

  // function related methods
  virtual Result<FunctionPtr> register_function(
      const RegisterFunctionParams &params) = 0;
  virtual Result<bool> register_function_list(
      const std::vector<RegisterFunctionParams> &params_list) = 0;
  virtual Result<bool> unregister_function(const std::string &name) = 0;
  virtual Result<bool> unregister_function_list(
      const std::vector<std::string> &name) = 0;
  virtual Result<FunctionPtr> get_function(const std::string &name) const = 0;
  virtual Result<FunctionPtrList> list_functions() const = 0;

  // execute is thread-safe, it returns a future, it may be executed in a
  // separate thread
  virtual std::future<Result<TablePtr>> execute(const Query &query) = 0;

  // helper function, used in functions or plugins for simplicity (a better
  // design would be seperate below methods into a different interface)
  virtual void report_error(const ReportErrorParams &params) = 0;
  virtual void logging(Logger::LogLevel level, const std::string &msg) = 0;
  virtual void set_logger(LoggerPtr logger) = 0;
};

using DatabasePtr = std::shared_ptr<Database>;

struct CreateDatabaseParams {};

Result<DatabasePtr> create_database(const CreateDatabaseParams &params);
}  // namespace lumidb
