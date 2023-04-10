#include "lumidb/function.hh"

#include <algorithm>
#include <any>
#include <cstddef>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fmt/core.h"
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
class Filters {
 public:
  void add_and_filter(Table::RowPredictor filter) {
    and_filters.emplace_back(std::move(filter));
  }

  bool perdict(const ValueList &row, size_t row_idx) const {
    for (auto &filter : and_filters) {
      if (!filter(row, row_idx)) {
        return false;
      }
    }
    return true;
  }

 private:
  std::vector<Table::RowPredictor> and_filters;
};

struct FieldNameUpdateItem {
  std::string field_name;
  AnyValue value;
};

struct FieldIndexUpdateItem {
  size_t field_index;
  AnyValue value;
};

struct CreateTableData {
  std::string name;
  TableSchema schema;
};

struct InsertData {
  TablePtr table;
  std::vector<ValueList> rows;
};

struct UpdateData {
  TablePtr table;
  Filters filters;
  vector<FieldNameUpdateItem> update_items;
};

struct DeleteData {
  TablePtr table;
  Filters filters;
};

struct QueryData {
  TablePtr table;
};

}  // namespace datas

// functions

class DescTableFunction : public helper::BaseRootFunction {
 public:
  DescTableFunction() : BaseFunction("desc_table") {
    set_signature({AnyType::from_string()});
    add_description("describe table");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto table_name = ctx.args[0].as_string();

    auto table_res = ctx.db->get_table(table_name);
    if (table_res.has_error()) {
      return table_res.unwrap_err();
    }
    auto table = table_res.unwrap();

    TableSchema out_schema;
    for (auto &field : table->schema().fields()) {
      out_schema.add_field(field.name, AnyType::from_string());
    }
    out_schema.add_field("rows", AnyType::from_float());

    auto out_table = Table::create_ptr("desc_table", out_schema);

    ValueList row;
    for (auto &field : table->schema().fields()) {
      row.emplace_back(field.type.name());
    }
    row.emplace_back(static_cast<float>(table->num_rows()));

    auto res1 = out_table->add_row(row);
    if (res1.has_error()) {
      return res1.unwrap_err();
    }

    ctx.result = out_table;

    return true;
  }
};

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
    schema.add_field("id", AnyType::from_string());
    schema.add_field("name", AnyType::from_string());
    schema.add_field("version", AnyType::from_string());
    schema.add_field("description", AnyType::from_string());
    schema.add_field("load_path", AnyType::from_string());

    auto table = Table::create_ptr("", schema);

    for (auto &p : plugins.unwrap()) {
      auto res = table->add_row(
          {p->id(), p->name(), p->version(), p->description(), p->load_path()});
      if (res.has_error()) {
        return res.unwrap_err();
      }
    }

    ctx.result = table;
    return true;
  }
};

// Plugins function
class LoadPluginFunction : public helper::BaseRootFunction {
 public:
  LoadPluginFunction() : BaseFunction("load_plugin") {
    set_signature({AnyType::from_string()});
    add_description("load plugin to the database");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto load_path = ctx.args[0].as_string();
    auto plugin_res = ctx.db->load_plugin(LoadPluginParams{load_path});
    if (plugin_res.has_error()) {
      return plugin_res.unwrap_err();
    }

    TableSchema schema;
    schema.add_field("id", AnyType::from_string());
    schema.add_field("name", AnyType::from_string());
    schema.add_field("version", AnyType::from_string());
    schema.add_field("description", AnyType::from_string());
    schema.add_field("load_path", AnyType::from_string());

    auto table = Table::create_ptr("", schema);

    auto p = plugin_res.unwrap();

    auto res2 = table->add_row(
        {p->id(), p->name(), p->version(), p->description(), p->load_path()});

    if (res2.has_error()) {
      return res2.unwrap_err();
    }

    ctx.db->logging(Logger::INFO, fmt::format("load plugin ok: {}", p->name()));
    ctx.result = table;
    return true;
  }
};

class UnloadPluginFunction : public helper::BaseRootFunction {
 public:
  UnloadPluginFunction() : BaseFunction("unload_plugin") {
    set_signature({AnyType::from_string()});
    add_description("unload plugin");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto plugin_id = ctx.args[0].as_string();

    auto res = ctx.db->unload_plugin(plugin_id);
    if (res.has_error()) {
      return res.unwrap_err();
    }

    // execute show_plugins
    auto show_res = ctx.db->execute({{{"show_plugins"}}});

    if (show_res.has_error()) {
      return show_res.unwrap_err();
    }

    ctx.db->logging(Logger::INFO,
                    fmt::format("unload plugin ok: {}", plugin_id));
    ctx.result = show_res.unwrap();

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

    auto out_res = ctx.db->execute({{{"desc_table", {data->name}}}});
    if (out_res.has_error()) {
      return out_res.unwrap_err();
    }

    ctx.result = out_res.unwrap();
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
        "add a field to the table. Supported types are `float`, `string`, "
        "`float?`, `string?`. The `?` means nullable.");
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

class SortFunction : public helper::BaseLeafFunction {
 public:
  SortFunction() : BaseFunction("sort") {
    set_signature_variadic(AnyType::from_string());

    add_description("sort fields of table asc (field1, field2, ...)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    if (ctx.args.size() == 0) {
      return Error("sort fields can not be empty");
    }

    auto field_names = value_list_to_strings(ctx.args);

    auto new_table_res = table->sort(field_names, true);

    if (new_table_res.has_error()) {
      return new_table_res.unwrap_err();
    }

    data->table = make_table_ptr(new_table_res.unwrap());

    return true;
  }
};

class SortDescFunction : public helper::BaseLeafFunction {
 public:
  SortDescFunction() : BaseFunction("sort_desc") {
    set_signature_variadic(AnyType::from_string());

    add_description("sort fields of table desc (field1, field2, ...)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    if (ctx.args.size() == 0) {
      return Error("sort fields can not be empty");
    }

    auto field_names = value_list_to_strings(ctx.args);

    auto new_table_res = table->sort(field_names, false);

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

    // root == Query
    if (auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
        data_res) {
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

    // root == Update
    if (auto data_res = any_cast_ptr<datas::UpdateData>(ctx.user_data);
        data_res) {
      auto data = data_res.value();
      auto table = data->table;

      auto field_idx_res = table->schema().get_field_index(field_name);
      if (field_idx_res.has_error()) {
        return field_idx_res.unwrap_err();
      }

      size_t field_idx = field_idx_res.unwrap();

      data->filters.add_and_filter([field_idx,
                                    comparator = std::move(comparator),
                                    value](const auto &row, auto row_idx) {
        auto field_value = row[field_idx];
        return comparator(field_value, value);
      });

      return true;
    }

    // root == Delete
    if (auto data_res = any_cast_ptr<datas::DeleteData>(ctx.user_data);
        data_res) {
      auto data = data_res.value();
      auto table = data->table;

      auto field_idx_res = table->schema().get_field_index(field_name);
      if (field_idx_res.has_error()) {
        return field_idx_res.unwrap_err();
      }

      size_t field_idx = field_idx_res.unwrap();

      data->filters.add_and_filter([field_idx,
                                    comparator = std::move(comparator),
                                    value](const auto &row, auto row_idx) {
        auto field_value = row[field_idx];
        return comparator(field_value, value);
      });

      return true;
    }

    return true;
  }
};

Result<TablePtr> handle_aggregation_function(
    std::string agg_func_name, TablePtr src_table,
    const std::vector<std::string> &field_names,
    function<void(AnyValue &acc, AnyValue elem)> agg_op,
    function<void(vector<AnyValue> &agg_results, TablePtr src_table,
                  const vector<size_t> &field_indices)>
        result_transformer = nullptr) {
  auto field_indices_res = src_table->schema().get_field_indices(field_names);
  if (field_indices_res.has_error()) {
    return field_indices_res.unwrap_err();
  }
  auto field_indices = field_indices_res.unwrap();

  vector<AnyValue> agg_results(field_indices.size());

  for (auto row_idx = 0; row_idx < src_table->num_rows(); row_idx++) {
    auto row = src_table->get_row(row_idx);
    for (auto i = 0; i < field_indices.size(); i++) {
      auto field_idx = field_indices[i];
      auto field_value = row[field_idx];
      agg_op(agg_results[i], field_value);
    }
  }

  if (result_transformer != nullptr) {
    result_transformer(agg_results, src_table, field_indices);
  }

  TableSchema out_schema;
  for (size_t i = 0; i < field_names.size(); i++) {
    auto name = fmt::format("{}({})", agg_func_name, field_names[i]);

    out_schema.add_field(name, agg_results[i].type());
  }

  auto out_table = Table::create_ptr("", out_schema);
  auto res1 = out_table->add_row(agg_results);
  if (res1.has_error()) {
    return res1.unwrap_err();
  }

  return out_table;
}

class AggMaxFunction : public helper::BaseLeafFunction {
 public:
  AggMaxFunction() : BaseFunction("max") {
    set_signature_variadic({AnyType::from_string()});

    add_description("aggregation max(field1, field2, ...)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    auto field_names = value_list_to_strings(ctx.args);

    auto out_res = handle_aggregation_function(
        "max", table, field_names, [](AnyValue &acc, AnyValue elem) {
          if (acc.is_null()) {
            acc = elem;
          } else {
            if (acc < elem) {
              acc = elem;
            }
          }
        });

    if (out_res.has_error()) {
      return out_res.unwrap_err();
    }

    data->table = out_res.unwrap();

    return true;
  }
};

class AggMinFunction : public helper::BaseLeafFunction {
 public:
  AggMinFunction() : BaseFunction("min") {
    set_signature_variadic({AnyType::from_string()});

    add_description("aggregation min(field1, field2, ...)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }

    auto data = data_res.value();
    auto table = data->table;

    auto field_names = value_list_to_strings(ctx.args);

    auto out_res = handle_aggregation_function(
        "min", table, field_names, [](AnyValue &acc, AnyValue elem) {
          if (acc.is_null()) {
            acc = elem;
          } else {
            if (!elem.is_null() && acc > elem) {
              acc = elem;
            }
          }
        });

    if (out_res.has_error()) {
      return out_res.unwrap_err();
    }

    data->table = out_res.unwrap();

    return true;
  }
};

class AggAvgFunction : public helper::BaseLeafFunction {
 public:
  AggAvgFunction() : BaseFunction("avg") {
    set_signature_variadic({AnyType::from_string()});

    add_description("aggregation avg(field)");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::QueryData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }

    auto data = data_res.value();
    auto table = data->table;

    auto field_names = value_list_to_strings(ctx.args);
    auto field_indices = table->schema().get_field_indices(field_names);
    if (field_indices.has_error()) {
      return field_indices.unwrap_err();
    }
    for (auto field_idx : field_indices.unwrap()) {
      auto &field = table->schema().get_field(field_idx);
      if (!field.type.is_null_float() && !field.type.is_float()) {
        return Error("invalid field type: {}, name: {}", field.type.name(),
                     field.name);
      }
    }

    auto out_res = handle_aggregation_function(
        "avg", table, field_names,
        [](AnyValue &acc, AnyValue elem) {
          if (acc.is_null()) {
            acc = elem;
          } else {
            acc = acc.as_float() + elem.as_float();
          }
        },
        [](vector<AnyValue> &agg_results, TablePtr table,
           const vector<size_t> &field_indices) {
          for (auto i = 0; i < agg_results.size(); i++) {
            float agg_result = 0;
            if (!agg_results[i].is_null()) {
              agg_result = agg_results[i].as_float();
            }
            agg_result = agg_result / table->num_rows();

            agg_results[i] = AnyValue::from_float(agg_result);
          }
        });

    if (out_res.has_error()) {
      return out_res.unwrap_err();
    }

    data->table = out_res.unwrap();

    return true;
  }
};

// Update Function

// Update
class UpdateRootFunction : public helper::BaseRootFunction {
 public:
  UpdateRootFunction()
      : BaseFunction("update",
                     FunctionSignature::make({AnyType::from_string()})) {
    add_description("update table");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    auto table_name = ctx.args[0].as_string();

    auto table_res = ctx.db->get_table(table_name);
    if (table_res.has_error()) {
      return table_res.unwrap_err();
    }

    auto data = std::make_shared<datas::UpdateData>();
    data->table = table_res.unwrap();
    ctx.user_data = data;

    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto data_res = any_cast_ptr<datas::UpdateData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid user data");
    }
    auto data = data_res.value();

    // transform updaters
    std::vector<datas::FieldIndexUpdateItem> field_updates;

    for (auto &field_name_update : data->update_items) {
      // resolve field name
      auto field_idx_res =
          data->table->schema().get_field_index(field_name_update.field_name);
      if (field_idx_res.has_error()) {
        return field_idx_res.unwrap_err();
      }

      auto field_idx = field_idx_res.unwrap();
      auto &field = data->table->schema().get_field(field_idx);

      // check type
      if (!field_name_update.value.is_instance_of(field.type)) {
        return Error("invalid type: {}, field: {}",
                     field_name_update.value.type().name(), field.name);
      }

      field_updates.push_back({field_idx, field_name_update.value});
    }

    auto u_res =
        data->table->update_row([&](ValueList &values, size_t row_idx) {
          if (data->filters.perdict(values, row_idx)) {
            for (auto &field_update : field_updates) {
              values[field_update.field_index] = field_update.value;
            }
          }
        });
    if (u_res.has_error()) {
      return u_res.unwrap_err();
    }

    ctx.result = data->table;
    return true;
  }
};

class SetValueFunction : public helper::BaseLeafFunction {
 public:
  SetValueFunction() : BaseFunction("set_value") {
    set_signature({AnyType::from_string(), AnyType::from_any()});
    add_description("set_value(field_name, value) update field value");
  }

  Result<bool> execute_leaf(LeafFunctionExecuteContext &ctx) override {
    auto data_res = any_cast_ptr<datas::UpdateData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid root func: {}", ctx.root_func->name());
    }
    auto data = data_res.value();
    auto table = data->table;

    auto field_name = ctx.args[0].as_string();
    auto value = ctx.args[1];

    data->update_items.push_back({field_name, value});

    return true;
  }
};

// Delete
class DeleteRootFunction : public helper::BaseRootFunction {
 public:
  DeleteRootFunction() : BaseFunction("delete") {
    set_signature({AnyType::from_string()});
    add_description("delete rows from table");
  }

  Result<bool> execute_root(RootFunctionExecuteContext &ctx) override {
    auto table_name = ctx.args[0].as_string();

    auto table_res = ctx.db->get_table(table_name);
    if (table_res.has_error()) {
      return table_res.unwrap_err();
    }

    auto data = std::make_shared<datas::DeleteData>();
    data->table = table_res.unwrap();
    ctx.user_data = data;

    return true;
  }

  Result<bool> finalize_root(RootFunctionFinalizeContext &ctx) override {
    auto data_res = any_cast_ptr<datas::DeleteData>(ctx.user_data);
    if (!data_res.has_value()) {
      return Error("invalid user data");
    }
    auto data = data_res.value();

    auto res =
        data->table->delete_rows([&](const ValueList &values, size_t row_idx) {
          return data->filters.perdict(values, row_idx);
        });
    if (res.has_error()) {
      return res.unwrap_err();
    }

    ctx.result = data->table;
    return true;
  }
};

class FunctionFactory {
 public:
  FunctionFactory() {
    register_function<ShowTablesFunction>();
    register_function<ShowFunctionsFunction>();
    register_function<ShowPluginsFunction>();
    register_function<DescTableFunction>();
    register_function<LoadPluginFunction>();
    register_function<UnloadPluginFunction>();
    register_function<CreateTableRootFunction>();
    register_function<AddFieldFunction>();
    register_function<UpdateRootFunction>();
    register_function<DeleteRootFunction>();
    register_function<SetValueFunction>();
    register_function<InsertRootFunction>();
    register_function<AddRowFunction>();
    register_function<LoadCSVFunction>();
    register_function<QueryRootFunction>();
    register_function<SelectFunction>();
    register_function<LimitFunction>();
    register_function<WhereFunction>();
    register_function<SortFunction>();
    register_function<SortDescFunction>();
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
