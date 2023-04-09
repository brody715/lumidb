#include <memory>
#include <string>

#include "db.hh"
#include "lumidb/types.hh"

namespace lumidb {
// Plugin Instance, not interface, a concrete class to manage loaded dll
class Plugin {
 public:
  ~Plugin() = default;

  // get plugin id
  plugin_id_t id() const { return id_; }

  // get plugin name
  std::string name() const { return ""; };

  // get plugin description
  std::string description() const { return ""; };

  // get plugin version
  std::string version() const { return ""; };

  std::string load_path() const { return ""; };

 private:
  // plugin id
  plugin_id_t id_;
};

Result<PluginPtr> load_plugin(plugin_id_t id, const LoadPluginParams &params);
}  // namespace lumidb
