#include <memory>

#include "db.hh"

#if LUMIDB_PLATFORM_WINDOWS
#include "windows.h"
namespace lumidb {
using symbol_address_t = FARPROC;
class DynamicLibraryInternal {
 public:
  DynamicLibraryInternal(HMODULE handle) : handle_(handle) {}

  static Result<std::unique_ptr<DynamicLibraryInternal>> load_from_path(
      const std::string &library_path) {
    HMODULE handle = LoadLibraryA(library_path.c_str());

    if (!handle) {
      return Error("failed to load library");
    }

    return std::make_unique<DynamicLibraryInternal>(handle);
  }

  ~DynamicLibraryInternal() {
    if (handle_) {
      FreeLibrary(handle_);
      handle_ = nullptr;
    }
  }

  symbol_address_t get_symbol_address(const std::string &symbol_name) {
    return GetProcAddress(handle_, symbol_name.c_str());
  }

 private:
  HMODULE handle_;
};
}  // namespace lumidb
#else
#include "dlfcn.h"

namespace lumidb {
using symbol_address_t = void *;
class DynamicLibraryInternal {
 public:
  DynamicLibraryInternal(void *handle) : handle_(handle) {}

  static Result<std::unique_ptr<DynamicLibraryInternal>> load_from_path(
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

  symbol_address_t get_symbol_address(const std::string &symbol_name) {
    return dlsym(handle_, symbol_name.c_str());
  }

  void *handle_ = nullptr;
};
}  // namespace lumidb
#endif

namespace lumidb {

// Simple wrapper for dynamic library across platforms
class DynamicLibrary {
 public:
  static Result<DynamicLibrary> load_from_path(const std::string &path) {
    auto res = DynamicLibraryInternal::load_from_path(path);
    if (!res) {
      return res.unwrap_err();
    }
    return DynamicLibrary(path, std::move(res.unwrap()));
  }
  ~DynamicLibrary() = default;
  DynamicLibrary(DynamicLibrary &&) = default;

  // Load a symbol from the library, get the address of the symbol
  symbol_address_t get_symbol_address(const std::string &symbol_name) {
    return internal_->get_symbol_address(symbol_name);
  };

  std::string load_path() const { return load_path_; }

 private:
  DynamicLibrary(std::string load_path_,
                 std::unique_ptr<DynamicLibraryInternal> internal)
      : load_path_(load_path_), internal_(std::move(internal)) {}

  std::string load_path_;

  std::unique_ptr<DynamicLibraryInternal> internal_;
};
}  // namespace lumidb