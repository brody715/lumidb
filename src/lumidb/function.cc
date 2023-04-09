#include "lumidb/function.hh"

#include <algorithm>
#include <any>
#include <cstddef>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "fmt/ostream.h"
#include "lumidb/db.hh"
#include "lumidb/plugin.hh"
#include "lumidb/table.hh"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"

using namespace lumidb;
using namespace std;

std::vector<std::string> value_list_to_strings(const ValueList &values) {
  std::vector<std::string> strings;
  for (auto &value : values) {
    strings.push_back(value.as_string());
  }
  return strings;
}

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

struct InsertData {
  TablePtr table;
  std::vector<ValueList> rows;
};

struct QueryData {
  TablePtr table;
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
    schema.add_field("id", AnyType::from_float());
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

// Plugins function
class LoadPluginFunction : public helper::BaseRootFunction {
 public:
  LoadPluginFunction()
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
    schema.add_field("id", AnyType::from_float());
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

// CreateTable

class CreateTableRootFunction : public helper::BaseRootFunction {
 public:
  CreateTableRootFunction()
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

// Insert

class InsertRootFunction : public helper::BaseRootFunction {
 public:
  InsertRootFunction()
      : BaseFunction("insert",
                     FunctionSignature::make({AnyType::from_string()})) {
    add_description("start to insert values to table");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    auto table_name = ctx.args[0].as_string();

    auto data = std::make_shared<datas::InsertData>();

    auto table_res = ctx.db->get_table(table_name);
    if (table_res.has_error()) {
      return table_res.unwrap_err();
    }

    data->table = table_res.unwrap();
    ctx.user_data = data;

    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    // auto data = std::any_cast()

    auto data_res = any_cast_ptr<datas::InsertData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid user data");
    }

    auto data = data_res.value();

    // get table
    auto res = data->table->add_row_list(data->rows);
    if (res.has_error()) {
      return res.unwrap_err();
    }

    ctx.result = data->table;
    return true;
  }
};

class AddRowFunction : public helper::BaseLeafFunction {
 public:
  AddRowFunction()
      : BaseFunction("add_row",
                     FunctionSignature::make_variadic({AnyType::from_any()})) {
    add_description("");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::InsertData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();

    auto table = data->table;

    // check schema
    if (table->schema().check_row(ctx.args).has_error()) {
      return Error("invalid row");
    }

    data->rows.emplace_back(ctx.args);

    return true;
  }
};

class LoadCSVFunction : public helper::BaseLeafFunction {
 public:
  LoadCSVFunction()
      : BaseFunction("load_csv",
                     FunctionSignature::make({AnyType::from_string()})) {
    add_description("load_csv from file");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::InsertData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();

    auto table = data->table;

    std::ifstream ifs(ctx.args[0].as_string());
    if (!ifs) {
      return Error("failed to open csv file: {}", ctx.args[0].as_string());
    }

    auto csv_res = parse_csv(ifs);
    if (!csv_res) {
      return csv_res.unwrap_err();
    }

    auto field_indices_res =
        table->schema().get_field_indices(csv_res->headers);
    if (!field_indices_res) {
      return field_indices_res.unwrap_err().add_message("invalid csv file");
    }

    auto field_indices = field_indices_res.unwrap();
    if (field_indices.size() != table->schema().fields_size()) {
      return Error("invalid csv file, field size mismatch");
    }

    auto &fields = table->schema().fields();

    std::vector<ValueList> rows;

    // start to insert
    for (size_t csv_row_idx = 0; csv_row_idx < csv_res->rows.size();
         csv_row_idx++) {
      ValueList row(field_indices.size());

      auto &csv_row = csv_res->rows[csv_row_idx];

      for (size_t i = 0; i < field_indices.size(); i++) {
        size_t field_index = field_indices[i];
        // parse value from string
        auto res = AnyValue::parse_from_string(fields[i].type, csv_row[i]);
        if (res.has_error()) {
          return res.unwrap_err().add_message(
              "failed to parse value from csv file, row_no={}, col_no={}, "
              "header={}, value={}",
              csv_row_idx, i, csv_res->headers[i], csv_row[i]);
        }

        row[field_index] = res.unwrap();
      }

      rows.emplace_back(std::move(row));
    }

    data->rows.insert(data->rows.end(), rows.begin(), rows.end());

    return true;
  }
};

// Query
class QueryRootFunction : public helper::BaseRootFunction {
 public:
  QueryRootFunction()
      : BaseFunction("query",
                     FunctionSignature::make({AnyType::from_string()})) {
    add_description("query table");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    auto table_name = ctx.args[0].as_string();

    auto data = std::make_shared<datas::QueryData>();

    auto table_res = ctx.db->get_table(table_name);
    if (table_res.has_error()) {
      return table_res.unwrap_err();
    }

    data->table = table_res.unwrap();
    ctx.user_data = data;

    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid user data");
    }
    auto data = data_res.value();

    ctx.result = data->table;
    return true;
  }
};

class SelectFunction : public helper::BaseLeafFunction {
 public:
  SelectFunction()
      : BaseFunction("select", FunctionSignature::make_variadic(
                                   {AnyType::from_string()})) {
    add_description("select fields of table");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    auto field_names = value_list_to_strings(ctx.args);

    auto select_table_res = table->select(field_names);
    if (select_table_res.has_error()) {
      return select_table_res.unwrap_err();
    }

    data->table = make_table_ptr(select_table_res.unwrap());

    return true;
  }
};

class LimitFunction : public helper::BaseLeafFunction {
 public:
  LimitFunction()
      : BaseFunction("limit",
                     FunctionSignature::make({AnyType::from_float()})) {
    add_description("limit return rows");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    auto limit = ctx.args[0].as_float();

    auto new_table_res = table->limit(0, limit);
    if (new_table_res.has_error()) {
      return new_table_res.unwrap_err();
    }

    data->table = make_table_ptr(new_table_res.unwrap());

    return true;
  }
};

class OrderByFunction : public helper::BaseLeafFunction {
 public:
  OrderByFunction() : BaseFunction("order_by") {
    set_signature({AnyType::from_string(), AnyType::from_string()});

    add_description("order_by fields of table (field, asc|desc)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    auto field_name = ctx.args[0].as_string();
    auto order = ctx.args[1].as_string();

    if (order != "asc" && order != "desc") {
      return Error("invalid order: {}", order);
    }

    auto new_table_res = table->sort({field_name}, order == "asc");

    if (new_table_res.has_error()) {
      return new_table_res.unwrap_err();
    }

    data->table = make_table_ptr(new_table_res.unwrap());

    return true;
  }
};

class WhereFunction : public helper::BaseLeafFunction {
 public:
  WhereFunction() : BaseFunction("where") {
    set_signature(
        {AnyType::from_string(), AnyType::from_string(), AnyType::from_any()});

    add_description(
        "where filter row, (<field>, <op>, <value>), support ('<', '=', '>') "
        "op currently");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    // parse args
    auto field_name = ctx.args[0].as_string();
    auto op = ctx.args[1].as_string();
    auto value = ctx.args[2];

    auto comparator_res = AnyValue::get_comparator(op);
    if (comparator_res.has_error()) {
      return comparator_res.unwrap_err();
    }

    auto comparator = comparator_res.unwrap();

    // get root function
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    auto field_idx_res = table->schema().get_field_index(field_name);
    if (field_idx_res.has_error()) {
      return field_idx_res.unwrap_err();
    }

    auto new_table_res = table->filter([&](const auto &row, auto row_idx) {
      auto field_value = row[field_idx_res.unwrap()];
      return comparator(field_value, value);
    });

    if (new_table_res.has_error()) {
      return new_table_res.unwrap_err();
    }

    data->table = make_table_ptr(new_table_res.unwrap());

    return true;
  }
};

class AggMaxFunction : public helper::BaseLeafFunction {
 public:
  AggMaxFunction() : BaseFunction("max") {
    set_signature({AnyType::from_string()});

    add_description("aggregation max(field)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    auto field_name = ctx.args[0].as_string();

    auto field_idx_res = table->schema().get_field_index(field_name);

    if (field_idx_res.has_error()) {
      return field_idx_res.unwrap_err();
    }

    auto field_idx = field_idx_res.unwrap();
    auto field = table->schema().get_field(field_idx);

    TableSchema schema;
    schema.add_field(fmt::format("max({})", field_name), field.type);

    AnyValue agg_result;

    for (auto row_idx = 0; row_idx < table->num_rows(); row_idx++) {
      auto row = table->get_row(row_idx);
      auto field_value = row[field_idx];

      if (agg_result.is_null()) {
        agg_result = field_value;
      } else {
        if (agg_result < field_value) {
          agg_result = field_value;
        }
      }
    }

    auto new_table = Table::create_ptr("", schema);
    new_table->add_row({agg_result});

    data->table = new_table;

    return true;
  }
};

class AggMinFunction : public helper::BaseLeafFunction {
 public:
  AggMinFunction() : BaseFunction("min") {
    set_signature({AnyType::from_string()});

    add_description("aggregation min(field)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }

    auto data = data_res.value();
    auto table = data->table;

    auto field_name = ctx.args[0].as_string();

    auto field_idx_res = table->schema().get_field_index(field_name);

    if (field_idx_res.has_error()) {
      return field_idx_res.unwrap_err();
    }

    auto field_idx = field_idx_res.unwrap();
    auto field = table->schema().get_field(field_idx);

    TableSchema schema;
    schema.add_field(fmt::format("min({})", field_name), field.type);

    AnyValue agg_result;

    for (auto row_idx = 0; row_idx < table->num_rows(); row_idx++) {
      auto row = table->get_row(row_idx);
      auto field_value = row[field_idx];

      if (agg_result.is_null()) {
        agg_result = field_value;
      } else {
        if (agg_result > field_value) {
          agg_result = field_value;
        }
      }
    }

    auto new_table = Table::create_ptr("", schema);
    new_table->add_row({agg_result});

    data->table = new_table;

    return true;
  }
};

class AggAvgFunction : public helper::BaseLeafFunction {
 public:
  AggAvgFunction() : BaseFunction("avg") {
    set_signature({AnyType::from_string()});

    add_description("aggregation avg(field)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }

    auto data = data_res.value();
    auto table = data->table;

    auto field_name = ctx.args[0].as_string();

    auto field_idx_res = table->schema().get_field_index(field_name);

    if (field_idx_res.has_error()) {
      return field_idx_res.unwrap_err();
    }

    auto field_idx = field_idx_res.unwrap();
    auto &field = table->schema().get_field(field_idx);

    if (!field.type.is_null_float() && !field.type.is_float()) {
      return Error("invalid field type: {}", field.type.name());
    }

    TableSchema schema;
    schema.add_field(fmt::format("avg({})", field_name), field.type);

    float agg_result = 0;

    for (auto row_idx = 0; row_idx < table->num_rows(); row_idx++) {
      auto row = table->get_row(row_idx);
      auto field_value = row[field_idx];

      if (!field_value.is_null()) {
        agg_result += field_value.as_float();
      }
    }

    agg_result /= table->num_rows();

    auto new_table = Table::create_ptr("", schema);
    new_table->add_row({agg_result});

    data->table = new_table;

    return true;
  }
};

// Query Aggregate Function

class FunctionFactory {
 public:
  FunctionFactory() {
    register_function<ShowTablesFunction>();
    register_function<ShowFunctionsFunction>();
    register_function<ShowPluginsFunction>();
    register_function<CreateTableRootFunction>();
    register_function<AddFieldFunction>();
    register_function<InsertRootFunction>();
    register_function<AddRowFunction>();
    register_function<LoadCSVFunction>();
    register_function<QueryRootFunction>();
    register_function<SelectFunction>();
    register_function<LimitFunction>();
    register_function<WhereFunction>();
    register_function<OrderByFunction>();
    register_function<AggAvgFunction>();
    register_function<AggMaxFunction>();
    register_function<AggMinFunction>();
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
