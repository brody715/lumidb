#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "db.hh"
#include "lumidb/plugin_def.hh"
#include "lumidb/types.hh"

namespace lumidb {

struct InternalLoadPluginParams {
  Database *db;
  plugin_id_t id;
  std::string path;
};

class DynamicLibrary;
// Plugin Instance, not interface, a concrete class to manage loaded dll
class Plugin {
 public:
  static Result<PluginPtr> load_plugin(const InternalLoadPluginParams &params);

  ~Plugin();

  Plugin(Plugin &&);
  Plugin &operator=(Plugin &&) = delete;

  // get plugin id
  plugin_id_t id() const { return id_; }

  // get plugin name
  std::string_view name() const { return def_->name; };

  // get plugin description
  std::string_view description() const { return def_->description; };

  // get plugin version
  std::string_view version() const { return def_->version; };

  std::string load_path() const;

 private:
  Plugin(plugin_id_t id, std::shared_ptr<DynamicLibrary> library)
      : id_(id), library_(std::move(library)) {}

 private:
  // plugin id
  plugin_id_t id_;
  LumiDBPluginContext ctx_;
  std::optional<LumiDBPluginDef> def_;
  std::shared_ptr<DynamicLibrary> library_;
};

}  // namespace lumidb
