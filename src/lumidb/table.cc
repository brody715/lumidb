
#include "lumidb/table.hh"

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

#include "tabulate/table.hpp"

using namespace std;
using namespace lumidb;

std::string transform_cell(const std::string &str) {
  if (str == "'null'") {
    return "null";
  }

  if (str == "null") {
    return "(缺省)";
  }

  // remove "'" from string
  if (str[0] == '\'' && str[str.size() - 1] == '\'') {
    return str.substr(1, str.size() - 2);
  }
  return str;
}

tabulate::Table::Row_t strings_to_tabulate_row(const vector<string> &values) {
  tabulate::Table::Row_t result;
  for (auto &value : values) {
    result.push_back(value);
  }
  return result;
}

struct RenderTableData {
  static RenderTableData from_table(const Table &table) {
    RenderTableData data;

    data.header = table.schema().field_names();

    for (auto &row : table.rows()) {
      vector<string> string_row;
      string_row.reserve(row.size());

      for (auto &value : row) {
        auto str = value.format_to_string();
        str = transform_cell(str);
        string_row.push_back(str);
      }

      data.rows.push_back(string_row);
    }

    return data;
  }

  std::ostream &dump(std::ostream &out) {
    tabulate::Table table_ui;

    table_ui.add_row(strings_to_tabulate_row(header));

    for (auto &row : rows) {
      table_ui.add_row(strings_to_tabulate_row(row));
    }

    auto max_column_size = get_column_max_size();
    for (size_t i = 0; i < header.size(); i++) {
      table_ui.column(i).format().multi_byte_characters(true).font_align(
          tabulate::FontAlign::left);

      if (max_column_size[i] > 40) {
        table_ui.column(i).format().width(40);
      }
    }

    for (size_t i = 0; i < header.size(); i++) {
      table_ui[0][i]
          .format()
          .font_align(tabulate::FontAlign::center)
          .font_style({tabulate::FontStyle::bold})
          .font_color(tabulate::Color::yellow);
    }

    out << table_ui;

    return out;
  }

 private:
  vector<size_t> get_column_max_size() {
    vector<size_t> result(header.size(), 0);

    for (auto &row : rows) {
      for (size_t i = 0; i < header.size(); i++) {
        result[i] = std::max(result[i], row[i].size());
      }
    }

    return result;
  }

 private:
  std::vector<std::string> header;
  vector<vector<string>> rows;
};

std::ostream &Table::dump(std::ostream &out) const {
  auto data = RenderTableData::from_table(*this);
  return data.dump(out);
}

std::ostream &lumidb::operator<<(std::ostream &out, const Table &table) {
  return table.dump(out);
}