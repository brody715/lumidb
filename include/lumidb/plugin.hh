#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "db.hh"
#include "lumidb/plugin_def.hh"
#include "lumidb/types.hh"

namespace lumidb {

class DynamicLibraryInternal;

// Simple wrapper for dynamic library across platforms
class DynamicLibrary {
 public:
  static Result<DynamicLibrary> load_from_path(const std::string &path);
  ~DynamicLibrary() = default;
  DynamicLibrary(DynamicLibrary &&) = default;

  // Load a symbol from the library, get the address of the symbol
  void *get_symbol_address(const std::string &symbol_name);

  std::string load_path() const { return load_path_; }

 private:
  DynamicLibrary(std::string load_path_,
                 std::shared_ptr<DynamicLibraryInternal> internal)
      : load_path_(load_path_), internal_(std::move(internal)) {}

  std::string load_path_;

  // It needs some workaround to use impl pattern using unique_ptr (call deleter
  // when moved), so we choose shared_ptr
  std::shared_ptr<DynamicLibraryInternal> internal_;
};

struct InternalLoadPluginParams {
  Database *db;
  plugin_id_t id;
  std::string path;
};

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

  std::string load_path() const { return library_.load_path(); };

 private:
  Plugin(plugin_id_t id, DynamicLibrary library)
      : id_(id), library_(std::move(library)) {}

 private:
  // plugin id
  plugin_id_t id_;
  LumiDBPluginContext ctx_;
  std::optional<LumiDBPluginDef> def_;
  DynamicLibrary library_;
};

}  // namespace lumidb
