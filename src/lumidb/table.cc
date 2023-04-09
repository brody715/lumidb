
#include "lumidb/table.hh"

#include <string>
#include <vector>

#include "tabulate/table.hpp"

using namespace std;
using namespace lumidb;

tabulate::Table::Row_t strings_to_tabulate_row(const vector<string> &values) {
  tabulate::Table::Row_t result;
  for (auto &value : values) {
    result.push_back(value);
  }
  return result;
}

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

tabulate::Table::Row_t values_to_tabulate_row(const vector<AnyValue> &values) {
  tabulate::Table::Row_t result;
  for (auto &value : values) {
    auto str = value.format_to_string();

    str = transform_cell(str);

    result.push_back(str);
  }
  return result;
}

std::ostream &Table::dump(std::ostream &out) const {
  tabulate::Table table_ui;

  auto header = schema_.field_names();
  table_ui.add_row(strings_to_tabulate_row(header));

  for (auto &row : rows_) {
    auto row_values = values_to_tabulate_row(row);
    table_ui.add_row(row_values);
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
