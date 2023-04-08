#pragma once

#include <memory>
#include <optional>
#include <stdexcept>

#include "lumidb/db.hh"
#include "lumidb/types.hh"

namespace lumidb {

struct LeafFunctionExecuteContext {
  // database
  DatabasePtr db;

  // function arguments
  std::vector<AnyValue> args;

  // prev function input table (can also used as result table)
  TablePtr prev_func_result;

  // result table
  std::optional<TablePtr> result;

  FunctionPtr root_func;
};

struct RootFunctionExecuteContext {
  // database
  DatabasePtr db;

  // function arguments
  std::vector<AnyValue> args;
};

struct RootFunctionFinalizeContext {
  // database
  DatabasePtr db;

  // function arguments
  std::vector<AnyValue> args;

  // last_func_result is the result table of the last function in the chain
  TablePtr last_func_result;
};

// Function Instance
class Function {
 public:
  // get function name
  virtual std::string name() const = 0;

  // can run as root function
  virtual bool can_root() const { return false; };

  // can run as leaf function
  virtual bool can_leaf() { return false; }

  // get function description
  virtual std::string description() const { return ""; };

  // If we have the func chain `query -> limit -> select`
  // then methods called are `query{execute_root} -> limit{execute_leaf} ->
  // select{execute_leaf}` -> `query{finalize_root}`

  // execute as leaf function
  virtual Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) {
    throw std::runtime_error("impossible route");
  }

  virtual Result<bool> execute_root(RootFunctionExecuteContext &ctx) {
    throw std::runtime_error("impossible route");
  }

  virtual Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) {
    throw std::runtime_error("impossible route");
  }
};
}  // namespace lumidb
