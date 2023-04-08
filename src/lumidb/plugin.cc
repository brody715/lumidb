#include "lumidb/plugin.hh"

#include "lumidb/types.hh"

using namespace std;
using namespace lumidb;

Result<PluginPtr> lumidb::load_plugin(plugin_id_t id,
                                      const LoadPluginParams &params) {
  return Error(Status::NOT_IMPLEMENTED);
}