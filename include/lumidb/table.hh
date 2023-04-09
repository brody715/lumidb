#pragma once

#include <algorithm>
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
  const std::vector<TableField> &fields() const { return fields_; }

  size_t fields_size() const { return fields_.size(); }

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

  const TableField &get_field(size_t idx) const { return fields_[idx]; }

  Result<size_t> get_field_index(const std::string &field_name) const {
    auto it = field_index_map_.find(field_name);
    if (it != field_index_map_.end()) {
      return it->second;
    }

    return Error("field not found: {}", field_name);
  }

  Result<std::vector<size_t>> get_field_indices(
      const std::vector<std::string> &field_names) const {
    std::vector<size_t> indices;
    for (auto &field_name : field_names) {
      auto res1 = get_field_index(field_name);
      if (res1.has_error()) {
        return res1.unwrap_err();
      }
      indices.push_back(res1.unwrap());
    }
    return indices;
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

  Result<bool> add_row_list(const std::vector<ValueList> &values_list) {
    for (auto &values : values_list) {
      auto res1 = schema_.check_row(values);
      if (res1.has_error()) {
        return res1.unwrap_err();
      }
    }

    rows_.insert(rows_.end(), values_list.begin(), values_list.end());

    return true;
  }

  Result<bool> add_row(const ValueList &values) {
    auto res1 = schema_.check_row(values);
    if (res1.has_error()) {
      return res1.unwrap_err();
    }

    rows_.push_back(values);

    return true;
  }

  // if predict return true, delete the row
  template <typename Predict>
  Result<bool> delete_rows(const Predict &predict) {
    std::vector<ValueList> new_rows;

    for (size_t i = 0; i < rows_.size(); i++) {
      auto &row = rows_[i];
      if (!predict(row, i)) {
        new_rows.push_back(row);
      }
    }

    rows_ = new_rows;
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

  size_t num_rows() const { return rows_.size(); }

  const ValueList &get_row(size_t row_index) const { return rows_[row_index]; }

  template <typename Predict>
  Result<Table> filter(const Predict &predict) const {
    Table new_table(name_, schema_);

    for (size_t i = 0; i < rows_.size(); i++) {
      auto &row = rows_[i];
      if (predict(row, i)) {
        new_table.rows_.push_back(row);
      }
    }

    return new_table;
  }

  // select fields by field indices, create new table
  Table select(const std::vector<size_t> &field_indices) const {
    TableSchema new_schema;
    for (auto field_index : field_indices) {
      new_schema.add_field(schema_.fields()[field_index].name,
                           schema_.fields()[field_index].type);
    }

    Table new_table(name_, new_schema);

    for (auto &row : rows_) {
      ValueList new_row;
      for (auto field_index : field_indices) {
        new_row.push_back(row[field_index]);
      }

      new_table.rows_.push_back(new_row);
    }

    return new_table;
  }

  // select fields by field names, create new table
  Result<Table> select(const std::vector<std::string> &field_names) const {
    auto res1 = schema_.get_field_indices(field_names);
    if (res1.has_error()) {
      return res1.unwrap_err();
    }

    return select(res1.unwrap());
  }

  // sort rows by field indices, create new table
  Result<Table> sort(const std::vector<size_t> &field_indices, bool asc) const {
    Table new_table = clone();

    std::sort(
        new_table.rows_.begin(), new_table.rows_.end(),
        [&](const ValueList &row1, const ValueList &row2) {
          for (auto field_index : field_indices) {
            if (field_index >= row1.size() || field_index >= row2.size()) {
              return false;
            }

            auto &value1 = row1[field_index];
            auto &value2 = row2[field_index];

            bool less_res = value1 < value2;
            if (less_res) {
              return asc;
            }

            bool greater_res = value1 > value2;
            if (greater_res) {
              return !asc;
            }
          }

          return false;
        });

    return new_table;
  }

  // sort rows by field names, create new table
  Result<Table> sort(const std::vector<std::string> &field_names,
                     bool asc) const {
    auto res1 = schema_.get_field_indices(field_names);
    if (res1.has_error()) {
      return res1.unwrap_err();
    }

    return sort(res1.unwrap(), asc);
  }

  Result<Table> limit(size_t offset, size_t count) const {
    Table new_table = Table(name_, schema_);

    for (size_t i = offset; i < rows_.size() && i < offset + count; i++) {
      new_table.rows_.push_back(rows_[i]);
    }

    return new_table;
  }

  template <typename AggFunction>
  Result<AnyValue> aggregate(const AggFunction &agg_func) {
    AnyValue agg_value;
    for (auto &row : rows_) {
      agg_value = agg_func(agg_value, row);
    }
    return agg_value;
  }

  // clone only clone name and schema, not rows
  Table clone_schema() const {
    Table table(name_, schema_);
    return table;
  }

  // clone a new table, used as a temporary table
  Table clone() const {
    Table table(name_, schema_);
    table.rows_ = rows_;
    return table;
  }

 private:
  std::string name_;
  TableSchema schema_{};

  std::vector<ValueList> rows_{};
};
}  // namespace lumidb
