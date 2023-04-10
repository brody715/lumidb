#pragma once

#include <any>
#include <memory>
#include <optional>
#include <string>

#include "lumidb/db.hh"
#include "lumidb/table.hh"
#include "lumidb/types.hh"

namespace lumidb {

class FunctionSignature {
 public:
  FunctionSignature() = default;

  static FunctionSignature make(std::vector<AnyType> param_types) {
    return FunctionSignature(param_types, false);
  }

  static FunctionSignature make_variadic(AnyType param_type) {
    return FunctionSignature({param_type}, true);
  }

  const std::vector<AnyType>& types() const { return types_; }
  bool is_variadic() const { return is_variadic_; }

  Result<bool> check(const ValueList& args) const {
    if (!is_variadic_ && args.size() != types_.size()) {
      return Error("arguments size mismatch, expected {}, got {}",
                   types_.size(), args.size());
    }

    // check variadic arguments
    if (is_variadic_) {
      // var schema should have exactly one type
      if (types_.size() != 1) {
        return Error(
            "schema error: variadic function should have exactly one type");
      }

      auto type = types_[0];

      for (size_t i = 0; i < args.size(); i++) {
        if (!args[i].is_instance_of(type)) {
          return Error("arg {} type mismatch, expected {}, got {}", i + 1,
                       type.name(), args[i].type().name());
        }
      }

      return true;
    }

    // check non-variadic arguments

    for (size_t i = 0; i < args.size(); i++) {
      if (!args[i].is_instance_of(types_[i])) {
        return Error("argument type mismatch, expected {}, got {}",
                     types_[i].name(), args[i].type().name());
      }
    }

    return true;
  }

 private:
  FunctionSignature(std::vector<AnyType> param_types, bool is_variadic)
      : types_(param_types), is_variadic_(is_variadic) {}

 private:
  std::vector<AnyType> types_;
  bool is_variadic_ = false;
};

std::ostream& operator<<(std::ostream& os, const FunctionSignature& sig);

struct LeafFunctionExecuteContext {
  // database
  Database* db;

  // function arguments
  ValueList args;

  // used to pass data between functions
  std::any user_data;

  FunctionPtr root_func;
};

struct RootFunctionExecuteContext {
  // database
  Database* db;

  // function arguments
  ValueList args;

  // used to pass data between functions
  std::any user_data;
};

struct RootFunctionFinalizeContext {
  // database
  Database* db;

  // function arguments
  ValueList args;

  // used to pass data between functions
  std::any user_data;

  // function result, can be set by root function
  std::optional<TablePtr> result;
};

// Function Interface
class Function {
 public:
  // get function name
  virtual std::string name() const = 0;

  // get function signature
  virtual const FunctionSignature& signature() const = 0;

  // can run as root function
  virtual bool can_root() const = 0;

  // can run as leaf function
  virtual bool can_leaf() const = 0;

  // get function description
  virtual std::string description() const = 0;

  // If we have the func chain `query -> limit -> select`
  // then methods called are `query{execute_root} -> limit{execute_leaf} ->
  // select{execute_leaf}` -> `query{finalize_root}`

  // execute as leaf function
  virtual Result<bool> execute_leaf(LeafFunctionExecuteContext& ctx) = 0;

  virtual Result<bool> execute_root(RootFunctionExecuteContext& ctx) = 0;

  virtual Result<bool> finalize_root(RootFunctionFinalizeContext& ctx) = 0;
};

std::vector<FunctionPtr> get_builtin_functions();

namespace helper {

std::string format_function(const Function& func);

// Helpers, to make it easier to define builtin functions
class BaseFunction : public Function {
 public:
  BaseFunction() = delete;
  virtual ~BaseFunction() = default;

  BaseFunction(std::string name) : name_(name) {}

  BaseFunction(std::string name, FunctionSignature signature)
      : name_(name), signature_(signature) {}

  std::string name() const override { return name_; }
  const FunctionSignature& signature() const override { return signature_; }
  std::string description() const override { return description_; }

  void set_name(std::string name) { name_ = name; }

  // set variadic signature
  void set_signature_variadic(AnyType param_type) {
    signature_ = FunctionSignature::make_variadic(param_type);
  }

  // set non variadic signature
  void set_signature(std::vector<AnyType> param_types) {
    signature_ = FunctionSignature::make(param_types);
  }

  void add_description(std::string description) { description_ += description; }

  bool can_leaf() const override { return false; }
  bool can_root() const override { return false; }

  Result<bool> execute_leaf(LeafFunctionExecuteContext& ctx) override {
    throw std::runtime_error("impossible route");
  }

  Result<bool> execute_root(RootFunctionExecuteContext& ctx) override {
    throw std::runtime_error("impossible route");
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext& ctx) override {
    throw std::runtime_error("impossible route");
  }

 protected:
  std::string name_;
  FunctionSignature signature_;
  std::string description_;
};

class BaseRootFunction : virtual public BaseFunction {
 public:
  bool can_root() const override { return true; }
  Result<bool> execute_root(RootFunctionExecuteContext& ctx) override = 0;
  Result<bool> finalize_root(RootFunctionFinalizeContext& ctx) override = 0;
};

class BaseLeafFunction : virtual public BaseFunction {
  bool can_leaf() const override { return true; }
  Result<bool> execute_leaf(LeafFunctionExecuteContext& ctx) override = 0;
};
}  // namespace helper

}  // namespace lumidb

template <>
struct fmt::formatter<lumidb::FunctionSignature> : fmt::ostream_formatter {};