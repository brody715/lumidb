#include "timer.hh"

#include <iostream>
#include <memory>
#include <string>

#include "lumidb/db.hh"
#include "lumidb/function.hh"
#include "lumidb/plugin_def.hh"
#include "lumidb/table.hh"
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
    auto res = ctx.db
                   ->execute(Query({
                       {"query", {table_name}},
                       {"where",
                        {field_name, AnyValue::from_string("="),
                         AnyValue::from_null()}},
                   }))
                   .get();

    if (res.has_error()) {
      return res.unwrap_err();
    }

    ctx.result = res.unwrap();
    return true;
  }
};

class AddTimerFunction : public helper::BaseRootFunction {
 public:
  AddTimerFunction(TimerManager *manager)
      : helper::BaseFunction("add_timer"), manager_(manager) {
    set_signature({AnyType::from_string(), AnyType::from_string()});
    add_description("timer-plugin: add_timer(<time-str>, <query-str>)");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto time_str = ctx.args[0].as_string();
    auto query_str = ctx.args[1].as_string();

    // try add timer
    auto res = manager_->add_timer(time_str, query_str);
    if (res.has_error()) {
      return res.unwrap_err();
    }

    auto out_res = ctx.db->execute(Query({{"show_timers"}})).get();
    if (out_res.has_error()) {
      return out_res.unwrap_err();
    }

    ctx.result = out_res.unwrap();

    ctx.db->logging(
        Logger::Info,
        fmt::format("timer-plugin: added timer: id={}", res.unwrap()));

    return true;
  }

 private:
  TimerManager *manager_;
};

class RemoveTimerFunction : public helper::BaseRootFunction {
 public:
  RemoveTimerFunction(TimerManager *manager)
      : helper::BaseFunction("remove_timer"), manager_(manager) {
    set_signature({AnyType::from_string()});
    add_description("timer-plugin: remove_timer(<timer-id>)");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto timer_id = ctx.args[0].as_string();

    // try add timer
    auto res = manager_->remove_timer(timer_id);
    if (res.has_error()) {
      return res.unwrap_err();
    }

    auto out_res = ctx.db->execute(Query({{"show_timers"}})).get();
    if (out_res.has_error()) {
      return out_res.unwrap_err();
    }

    ctx.result = out_res.unwrap();

    ctx.db->logging(
        Logger::Info,
        fmt::format("timer-plugin: removed timer: id={}", timer_id));

    return true;
  }

 private:
  TimerManager *manager_;
};

class ShowTimersFunction : public helper::BaseRootFunction {
 public:
  ShowTimersFunction(TimerManager *manager)
      : helper::BaseFunction("show_timers"), manager_(manager) {
    set_signature({});
    add_description("timer-plugin: show_timers()");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    auto timers = manager_->list_timer_descs();

    TableSchema schema;
    schema.add_field("id", AnyType::from_string());
    schema.add_field("interval", AnyType::from_string());
    schema.add_field("query", AnyType::from_string());

    TablePtr table = Table::create_ptr("timers", schema);

    for (auto &timer : timers) {
      auto res =
          table->add_row({timer.id, timer.time_string, timer.query_string});
      if (res.has_error()) {
        return res.unwrap_err();
      }
    }

    return helper::execute_query_root(ctx, table);
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    return helper::finalize_query_root(ctx);
  }

 private:
  TimerManager *manager_;
};

TimerPlugin::TimerPlugin(lumidb::Database *db)
    : db_(db), manager_(std::make_shared<TimerManager>(db_)) {}

int TimerPlugin::on_load() {
  auto res = db_->register_function_list(
      {{make_function_ptr<FindMissingValuesFunction>()},
       {make_function_ptr<AddTimerFunction>(manager_.get())},
       {make_function_ptr<RemoveTimerFunction>(manager_.get())},
       {make_function_ptr<ShowTimersFunction>(manager_.get())}});

  if (res.has_error()) {
    db_->report_error({
        .source = "plugin",
        .name = "timer-plugin",
        .error = res.unwrap_err(),
    });
    return 1;
  }

  return 0;
}

TimerPlugin::~TimerPlugin() {
  if (db_ == nullptr) {
    return;
  }

  auto res = db_->unregister_function_list(
      {"find_missing_values", "add_timer", "remove_timer", "show_timers"});

  if (res.has_error()) {
    db_->report_error({
        .source = "plugin",
        .name = "timer-plugin",
        .error = res.unwrap_err(),
    });
  }

  db_ = nullptr;
}

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
          delete plugin;
          ctx->user_data = nullptr;
        }

        return rc;
      }};
  return def;
}
}
