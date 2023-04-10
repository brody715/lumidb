#include <iostream>
#include <string>

#include "lumidb/db.hh"
#include "lumidb/function.hh"
#include "lumidb/plugin_def.hh"
#include "lumidb/types.hh"

using namespace lumidb;
using namespace std;

class FindMissingValuesFunction : public helper::BaseRootFunction {
 public:
  FindMissingValuesFunction() : helper::BaseFunction("find_missing_values") {
    set_signature({AnyType::from_string(), AnyType::from_string()});
    add_description("timer-plugin: find_missing_values(<table>, <field>)");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto table_name = ctx.args[0];
    auto field_name = ctx.args[1];

    // just reexecute
    auto res = ctx.db->execute(Query({
        {"query", {table_name}},
        {"where",
         {field_name, AnyValue::from_string("="), AnyValue::from_null()}},
    }));

    if (res.has_error()) {
      return res.unwrap_err();
    }

    ctx.result = res.unwrap();
    return true;
  }
};

class TimerPlugin {
 public:
  TimerPlugin(lumidb::Database *db) : db(db) {}
  ~TimerPlugin() {}

  int on_load() {
    auto res = db->register_function_list(
        {{make_function_ptr<FindMissingValuesFunction>()}});

    if (res.has_error()) {
      db->report_error({
          .source = "plugin",
          .name = "timer-plugin",
          .error = res.unwrap_err(),
      });
      return 1;
    }

    return 0;
  }

  int unload() {
    auto res = db->unregister_function_list({
        "find_missing_values",
    });

    if (res.has_error()) {
      db->report_error({
          .source = "plugin",
          .name = "timer-plugin",
          .error = res.unwrap_err(),
      });
      return 1;
    }

    return 0;
  }

 private:
  lumidb::Database *db;
};

extern "C" {
LumiDBPluginDef LUMI_DB_ATTRIBUTE_WEAK lumi_db_get_plugin_def() {
  static LumiDBPluginDef def = {
      "timer-plugin", "0.0.1", "LumiDB Timer Plugin",
      [](LumiDBPluginContext *ctx) {
        // cast to lumidb::Database
        auto db = reinterpret_cast<lumidb::Database *>(ctx->db);
        if (db == nullptr) {
          ctx->error = "failed to cast to lumidb::Database";
          return 1;
        }

        auto plugin = new TimerPlugin(db);
        ctx->user_data = plugin;
        int ret = plugin->on_load();

        if (ret != 0) {
          delete plugin;
          ctx->user_data = nullptr;
        }

        return ret;
      },
      [](LumiDBPluginContext *ctx) {
        int rc = 0;
        if (ctx->user_data != nullptr) {
          TimerPlugin *plugin = reinterpret_cast<TimerPlugin *>(ctx->user_data);
          rc = plugin->unload();
          delete plugin;
          ctx->user_data = nullptr;
        }

        return rc;
      }};
  return def;
}
}
