#include "lumidb/plugin.hh"

#include <memory>

#include "lumidb/db.hh"
#include "lumidb/plugin_def.hh"
#include "lumidb/types.hh"

using namespace std;
using namespace lumidb;

#ifdef LUMIDB_PLATFORM_WINDOWS

#elif LUMIDB_PLATFORM_LINUX
#include "dlfcn.h"

namespace lumidb {
class DynamicLibraryInternal {
 public:
  DynamicLibraryInternal(void *handle) : handle_(handle) {}

  static Result<std::unique_ptr<DynamicLibraryInternal> > load_from_path(
      const std::string &library_path) {
    void *handle = dlopen(library_path.c_str(), RTLD_LAZY);

    if (!handle) {
      return Error(dlerror());
    }

    return std::make_unique<DynamicLibraryInternal>(handle);
  }

  ~DynamicLibraryInternal() {
    if (handle_) {
      dlclose(handle_);
      handle_ = nullptr;
    }
  }

  void *get_symbol_address(const std::string &symbol_name) {
    return dlsym(handle_, symbol_name.c_str());
  }

  void *handle_;
};
}  // namespace lumidb

#endif

Result<DynamicLibrary> DynamicLibrary::load_from_path(
    const std::string &library_path) {
  auto res = DynamicLibraryInternal::load_from_path(library_path);
  if (!res) {
    return res.unwrap_err();
  }
  return DynamicLibrary(library_path, std::move(res.unwrap()));
}

void *DynamicLibrary::get_symbol_address(const std::string &symbol_name) {
  return internal_->get_symbol_address(symbol_name);
}

Result<PluginPtr> Plugin::load_plugin(const InternalLoadPluginParams &params) {
  auto lib_res = DynamicLibrary::load_from_path(params.path);
  if (!lib_res) {
    return lib_res.unwrap_err().add_message("failed to load plugin library");
  }

  auto plugin =
      std::make_shared<Plugin>(Plugin(params.id, std::move(lib_res.unwrap())));

  void *plugin_func_ptr =
      plugin->library_.get_symbol_address("lumi_db_get_plugin_def");
  if (plugin_func_ptr == nullptr) {
    return Error(
        "failed to find symbol `lumi_db_get_plugin_def` in plugin, "
        "please check if the plugin is valid");
  }

  auto plugin_func =
      reinterpret_cast<decltype(lumi_db_get_plugin_def) *>(plugin_func_ptr);

  plugin->ctx_ = LumiDBPluginContext{
      .user_data = nullptr,
      .db = params.db,
      .error = nullptr,
  };

  plugin->def_ = plugin_func();

  if (!plugin->def_) {
    return Error("failed to get plugin definition");
  }

  if (!plugin->def_->on_load) {
    return Error("plugin definition does not have on_load function");
  }

  if (plugin->def_->on_load(&plugin->ctx_) != 0) {
    return Error("failed to load plugin: {}",
                 plugin->ctx_.error == nullptr ? "" : plugin->ctx_.error);
  }

  return plugin;
}

Plugin::~Plugin() {
  if (def_.has_value()) {
    if (def_->on_unload) {
      def_->on_unload(&ctx_);
    }

    def_ = std::nullopt;
  }
}

Plugin::Plugin(Plugin &&other)
    : id_(other.id_),
      library_(std::move(other.library_)),
      def_(std::move(other.def_)),
      ctx_(std::move(other.ctx_)) {}