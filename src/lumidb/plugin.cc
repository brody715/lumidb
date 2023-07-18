#include "lumidb/plugin.hh"

#include <memory>
#include <string>

#include "lumidb/db.hh"
#include "lumidb/dynamic_library.hh"
#include "lumidb/plugin_def.hh"
#include "lumidb/types.hh"

using namespace lumidb;

std::string Plugin::load_path() const { return library_->load_path(); }

Result<PluginPtr> Plugin::load_plugin(const InternalLoadPluginParams &params) {
  auto lib_res = DynamicLibrary::load_from_path(params.path);
  if (!lib_res) {
    return lib_res.unwrap_err().add_message("failed to load plugin library");
  }

  auto lib_ptr = std::make_shared<DynamicLibrary>(std::move(lib_res.unwrap()));

  auto plugin = std::make_shared<Plugin>(Plugin(params.id, std::move(lib_ptr)));

  symbol_address_t plugin_func_ptr =
      plugin->library_->get_symbol_address("lumi_db_get_plugin_def");
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