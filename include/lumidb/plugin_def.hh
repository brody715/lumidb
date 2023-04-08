#pragma once

#define LUMI_DB_ATTRIBUTE_WEAK __attribute__((weak))

namespace lumidb {
extern "C" {

struct LumiDBPluginContext {
  // user_data is used to store plugin's private data
  void *user_data;

  // db is used to access lumidb's database, it should be dynamic cast to
  // lumidb::Database
  void *db;

  // error is used to store error message (when on_load or on_unload returns
  // non-zero)
  const char *error;
};

struct LumiDBPluginDef {
  const char *name;
  const char *version;
  const char *description;
  int (*on_load)(LumiDBPluginContext *ctx);
  int (*on_unload)(LumiDBPluginContext *ctx);
};
}
}  // namespace lumidb

extern "C" ::lumidb::LumiDBPluginDef LUMI_DB_ATTRIBUTE_WEAK
lumi_db_get_plugin_def();
