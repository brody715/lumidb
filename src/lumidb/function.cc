#include "lumidb/function.hh"

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "fmt/ostream.h"
#include "lumidb/db.hh"
#include "lumidb/plugin.hh"
#include "lumidb/table.hh"
#include "lumidb/types.hh"

using namespace lumidb;
using namespace std;

std::ostream &lumidb::operator<<(std::ostream &os,
                                 const FunctionSignature &sig) {
  std::vector<std::string> param_types;

  for (auto &type : sig.types()) {
    param_types.push_back(type.name());
  }

  if (sig.is_variadic()) {
    param_types.push_back("...");
  }

  os << fmt::format("({})", fmt::join(param_types, ", "));

  return os;
}

// helper functions
std::string lumidb::helper::format_function(const Function &func) {
  return fmt::format("{}{}", func.name(), func.signature());
}

template <typename T>
using Ptr = std::shared_ptr<T>;

template <typename T>
optional<Ptr<T>> any_cast_ptr(std::any value) {
  if (value.type() == typeid(Ptr<T>)) {
    return std::any_cast<Ptr<T>>(value);
  }
  return {};
}

namespace datas {
struct CreateTableData {
  std::string name;
  TableSchema schema;
};
}  // namespace datas

// functions

class ShowTablesFunction : public helper::BaseRootFunction {
 public:
  ShowTablesFunction()
      : BaseFunction("show_tables", FunctionSignature::make({})) {
    add_description("show tables in the database");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto tables = ctx.db->list_tables();
    if (tables.has_error()) {
      return tables.unwrap_err();
    }

    TableSchema schema;
    schema.add_field("name", AnyType::from_string());

    auto table = Table::create_ptr("show_tables", schema);

    for (auto &t : tables.unwrap()) {
      table->add_row({t->name()});
    }

    ctx.result = table;
    return true;
  }
};

class ShowFunctionsFunction : public helper::BaseRootFunction {
 public:
  ShowFunctionsFunction()
      : BaseFunction("show_functions", FunctionSignature::make({})) {
    add_description("show functions in the database");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto functions = ctx.db->list_functions();
    if (functions.has_error()) {
      return functions.unwrap_err();
    }

    TableSchema schema;
    schema.add_field("signature", AnyType::from_string());
    schema.add_field("type", AnyType::from_string());
    schema.add_field("description", AnyType::from_string());

    auto table = Table::create_ptr("", schema);

    for (auto &f : functions.unwrap()) {
      string signature = helper::format_function(*f);
      string type = f->can_root() ? "root" : "leaf";

      table->add_row({signature, type, f->description()});
    }

    ctx.result = table;
    return true;
  }
};

class ShowPluginsFunction : public helper::BaseRootFunction {
 public:
  ShowPluginsFunction()
      : BaseFunction("show_plugins", FunctionSignature::make({})) {
    add_description("show plugins in the database");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto plugins = ctx.db->list_plugins();
    if (plugins.has_error()) {
      return plugins.unwrap_err();
    }

    TableSchema schema;
    schema.add_field("id", AnyType::from_int());
    schema.add_field("name", AnyType::from_string());
    schema.add_field("version", AnyType::from_string());
    schema.add_field("description", AnyType::from_string());
    schema.add_field("load_path", AnyType::from_string());

    auto table = Table::create_ptr("", schema);

    for (auto &p : plugins.unwrap()) {
      table->add_row(
          {p->id(), p->name(), p->version(), p->description(), p->load_path()});
    }

    ctx.result = table;
    return true;
  }
};

class CreateTableFunction : public helper::BaseRootFunction {
 public:
  CreateTableFunction()
      : BaseFunction("create_table",
                     FunctionSignature::make({AnyType::from_string()})) {
    add_description(
        "create a table. Use like `create_table(\"stu\") | add_field(\"name\", "
        "\"string\")`");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    if (ctx.args.size() != 1) {
      return Error("create_table requires 1 argument");
    }

    auto arg = ctx.args[0];
    if (!arg.is_string()) {
      return Error("create_table requires string argument");
    }

    auto data = std::make_shared<datas::CreateTableData>();
    data->name = arg.as_string();

    ctx.user_data = data;

    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    // auto data = std::any_cast()

    auto data_res = any_cast_ptr<datas::CreateTableData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid user data");
    }

    auto data = data_res.value();

    if (data->schema.fields().empty()) {
      return Error("schema is empty");
    }

    auto table = Table::create_ptr(data->name, data->schema);

    auto res = ctx.db->create_table({table});
    if (res.has_error()) {
      return res.unwrap_err();
    }

    ctx.result = table;
    return true;
  }
};

class AddFieldFunction : public helper::BaseLeafFunction {
 public:
  AddFieldFunction()
      : BaseFunction("add_field",
                     FunctionSignature::make(
                         {AnyType::from_string(), AnyType::from_string()})) {
    add_description(
        "add a field to the table. Supported types are `int`, `string`, "
        "`int?`, `string?`. The `?` means nullable.");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::CreateTableData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();

    if (ctx.args.size() != 2) {
      return Error("requires 2 argument");
    }

    auto field_name = ctx.args[0].as_string();
    auto field_type_res = AnyType::parse_string(ctx.args[1].as_string());

    if (field_type_res.has_error()) {
      return field_type_res.unwrap_err();
    }
    data->schema.add_field(field_name, field_type_res.unwrap());

    return true;
  }
};

class FunctionFactory {
 public:
  FunctionFactory() {
    register_function<ShowTablesFunction>();
    register_function<ShowFunctionsFunction>();
    register_function<ShowPluginsFunction>();
    register_function<CreateTableFunction>();
    register_function<AddFieldFunction>();
  }

  // register function
  template <typename T>
  void register_function() {
    register_function(std::make_shared<T>());
  }

  void register_function(FunctionPtr func) { functions_.emplace_back(func); }
  std::vector<FunctionPtr> get_functions() const { return functions_; }

 private:
  std::vector<FunctionPtr> functions_;
};

static FunctionFactory builtin_function_factory;

std::vector<FunctionPtr> lumidb::get_builtin_functions() {
  return builtin_function_factory.get_functions();
}
