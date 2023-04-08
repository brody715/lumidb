#pragma once

#include <memory>
#include <string>
#include <vector>

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

struct ReportErrorParams {
  std::string source;
  std::string name;
  Error error;
};

struct CreateTableParams {
  std::string name;
};

struct LoadPluginParams {
  std::string path;
};

struct RegisterFunctionParams {
  FunctionPtr func;
};

// Database Interface, can be accessed by plugins, functions ...
class Database {
 public:
  Database() = default;
  virtual ~Database() = default;

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
  virtual Result<bool> unregister_function(const std::string &name) = 0;
  virtual Result<FunctionPtr> get_function(const std::string &name) const = 0;
  virtual Result<FunctionPtrList> list_functions() const = 0;

  // execute query, return result as a table
  virtual Result<TablePtr> execute(const Query &query) = 0;

  // helper function
  virtual void report_error(const ReportErrorParams &params) = 0;
};

using DatabasePtr = std::shared_ptr<Database>;

struct CreateDatabaseParams {};

DatabasePtr create_database(const CreateDatabaseParams &params);
}  // namespace lumidb