#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "db.hh"
#include "lumidb/types.hh"

namespace lumidb {

using ValueList = std::vector<AnyValue>;
using RowIndicesList = std::vector<size_t>;

struct TableField {
  std::string name;
  AnyType type;
};

class TableSchema {
 public:
  std::vector<TableField> fields() const { return fields_; }

  std::vector<std::string> field_names() const {
    std::vector<std::string> names;
    for (auto &field : fields_) {
      names.push_back(field.name);
    }
    return names;
  }

  Result<bool> add_field(const std::string &name, const AnyType &type) {
    size_t idx = fields_.size();

    auto [_, ok] = field_index_map_.insert({name, idx});
    if (!ok) {
      return Error("field already exists: {}", name);
    }
    fields_.push_back({name, type});

    return true;
  }

  Result<size_t> get_field_index(const std::string &field_name) const {
    auto it = field_index_map_.find(field_name);
    if (it != field_index_map_.end()) {
      return it->second;
    }

    return Error("field not found: {}", field_name);
  }

  Result<bool> check_row(const std::vector<AnyValue> &values) const {
    if (values.size() != fields_.size()) {
      return Error("row size not matched with schema");
    }

    for (size_t i = 0; i < values.size(); i++) {
      if (!values[i].type().is_subtype_of(fields_[i].type)) {
        return Error(
            "field type not matched with schema, field: {}, type: {}, value: "
            "{}",
            fields_[i].name, fields_[i].type.name(), values[i].type().name());
      }
    }

    return true;
  }

 private:
  std::vector<TableField> fields_;
  std::map<std::string, size_t> field_index_map_;
};

class Table {
 public:
  Table(std::string name, TableSchema schema) : name_(name), schema_(schema) {}

  static TablePtr create_ptr(std::string name, TableSchema schema) {
    return std::make_shared<Table>(name, schema);
  }

  const std::string &name() const { return name_; }
  const TableSchema &schema() const { return schema_; }
  const std::vector<ValueList> &rows() const { return rows_; }

  Result<bool> add_row(const ValueList &values) {
    auto res1 = schema_.check_row(values);
    if (res1.has_error()) {
      return res1.unwrap_err();
    }

    rows_.push_back(values);

    return true;
  }

  Result<bool> delete_rows(const std::vector<size_t> &row_indices) {
    throw std::runtime_error("not implemented");
  }

  Result<bool> update_field_value(const std::vector<size_t> &row_indices,
                                  size_t field_index, const AnyValue &value) {
    for (auto row_index : row_indices) {
      if (row_index >= rows_.size()) {
        return Error("row index out of range");
      }

      auto &row = rows_[row_index];
      if (field_index >= row.size()) {
        return Error("field index out of range");
      }

      row[field_index] = value;
    }

    return true;
  }

  std::ostream &dump(std::ostream &out) const;

 private:
  std::string name_;
  TableSchema schema_{};

  std::vector<ValueList> rows_{};
};
}  // namespace lumidb
