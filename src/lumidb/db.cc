#include "lumidb/db.hh"

#include <iostream>
#include <map>
#include <vector>

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
      const CreateTableParams &params) override {
    auto [_, ok] = tables_.insert({params.table->name(), params.table});
    if (!ok) {
      return Error("table already exists: {}", params.table->name());
    }
    return params.table;
  }
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
      return plugin.unwrap_err();
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
    return _register_function(params);
  }

  virtual Result<bool> register_function_list(
      const std::vector<RegisterFunctionParams> &params_list) override {
    for (auto &params : params_list) {
      auto func = _register_function(params);
      if (func.has_error()) {
        return func.unwrap_err();
      }
    }
    return true;
  }

  virtual Result<bool> unregister_function(const std::string &name) override {
    functions_.erase(name);
    return true;
  }
  virtual Result<FunctionPtr> get_function(
      const std::string &name) const override {
    return _get_function(name);
  }
  virtual Result<FunctionPtrList> list_functions() const override {
    FunctionPtrList functions;
    for (auto &it : functions_) {
      functions.push_back(it.second);
    }
    return functions;
  }

  // execute query, return result as a table
  virtual Result<TablePtr> execute(const Query &query) override {
    // resolve function and its arguments
    std::vector<FunctionPtr> funcs;
    std::vector<ValueList> args_list;

    funcs.reserve(query.functions.size());
    args_list.reserve(query.functions.size());

    for (auto &func : query.functions) {
      auto func_ptr = _get_function(func.name);
      if (func_ptr.has_error()) {
        return func_ptr.unwrap_err().add_message(
            "failed to resolve function: {}", func.name);
      }
      funcs.push_back(func_ptr.unwrap());

      args_list.push_back(func.arguments);
    }

    // check we have at least one function
    if (funcs.empty()) {
      return Error("no function to execute");
    }

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
      return res.unwrap_err().add_message("failed to execute root function: {}",
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
        return res.unwrap_err().add_message(
            "failed to execute leaf function: {}", func->name());
      }
    }

    // finalize root function
    root_final_ctx.user_data = leaf_exec_ctx.user_data;
    res = root_func->finalize_root(root_final_ctx);
    if (res.has_error()) {
      return res.unwrap_err().add_message(
          "failed to finalize root function: {}", root_func->name());
    }

    if (root_final_ctx.result.has_value()) {
      return root_final_ctx.result.value();
    }

    // we just return empty table
    return std::make_shared<Table>("", TableSchema{});
  }

  // helper function
  virtual void report_error(const ReportErrorParams &params) override {
    // TODO: use error report register
    std::cerr << params.source << ": " << params.name << ": "
              << params.error.message;
  }

 private:
  Result<FunctionPtr> _get_function(const std::string &name) const {
    auto it = functions_.find(name);
    if (it == functions_.end()) {
      return Error("function not found: {}", name);
    }
    return it->second;
  }

  Result<FunctionPtr> _register_function(const RegisterFunctionParams &params) {
    functions_[params.func->name()] = params.func;
    return params.func;
  }

 private:
  map<string, TablePtr> tables_;
  map<string, FunctionPtr> functions_;
  map<int, PluginPtr> plugins_;

  IdGenerator plugin_id_gen_;
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
