#include "lumidb/db.hh"

#include <iostream>
#include <map>
#include <unordered_map>

#include "lumidb/function.hh"
#include "lumidb/plugin.hh"
#include "lumidb/table.hh"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"

using namespace std;
using namespace lumidb;

// Database in memory
class MemoryDatabase : public lumidb::Database {
 public:
  MemoryDatabase() = default;

  // table related methods
  virtual Result<TablePtr> create_table(
      const CreateTableParams &params) override {}
  virtual Result<bool> drop_table(const std::string &name) override {
    tables_.erase(name);
    return true;
  }
  virtual Result<TablePtr> get_table(const std::string &name) const override {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
      return Error("table not found: {}", name);
    }
    return it->second;
  }
  virtual Result<TablePtrList> list_tables() const override {
    TablePtrList tables;
    for (auto &it : tables_) {
      tables.push_back(it.second);
    }
    return tables;
  }

  // plugin related methods
  virtual Result<PluginPtr> load_plugin(
      const LoadPluginParams &params) override {
    auto id = plugin_id_gen_.next_id();
    auto plugin = lumidb::load_plugin(id, params);
    if (plugin.has_error()) {
      return plugin.error();
    }

    plugins_[id] = plugin.unwrap();

    return plugin.unwrap();
  }

  virtual Result<bool> unload_plugin(int id) override {
    plugins_.erase(id);
    return true;
  }
  virtual Result<PluginPtr> get_plugin(int id) const override {
    auto it = plugins_.find(id);
    if (it == plugins_.end()) {
      return Error("plugin not found: {}", id);
    }
    return it->second;
  }
  virtual Result<PluginPtrList> list_plugins() const override {
    PluginPtrList plugins;
    for (auto &it : plugins_) {
      plugins.push_back(it.second);
    }
    return plugins;
  }

  // function related methods
  virtual Result<FunctionPtr> register_function(
      const RegisterFunctionParams &params) override {
    functions_[params.func->name()] = params.func;
    return params.func;
  }
  virtual Result<bool> unregister_function(const std::string &name) override {
    functions_.erase(name);
    return true;
  }
  virtual Result<FunctionPtr> get_function(
      const std::string &name) const override {
    auto it = functions_.find(name);
    if (it == functions_.end()) {
      return Error("function not found: {}", name);
    }
    return it->second;
  }
  virtual Result<FunctionPtrList> list_functions() const override {
    FunctionPtrList functions;
    for (auto &it : functions_) {
      functions.push_back(it.second);
    }
    return functions;
  }

  // execute query, return result as a table
  virtual Result<TablePtr> execute(const Query &query) override {}

  // helper function
  virtual void report_error(const ReportErrorParams &params) override {
    // TODO: use error report register
    std::cerr << params.source << ": " << params.name << ": "
              << params.error.message;
  }

 private:
  unordered_map<string, TablePtr> tables_;
  unordered_map<string, FunctionPtr> functions_;
  unordered_map<int, PluginPtr> plugins_;

  IdGenerator plugin_id_gen_;
};

DatabasePtr create_database(const CreateDatabaseParams &params) {
  return std::make_shared<MemoryDatabase>();
}
