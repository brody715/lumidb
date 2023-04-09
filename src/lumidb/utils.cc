#include "lumidb/utils.hh"

#include <cctype>
#include <optional>
#include <string>

#include "lumidb/types.hh"

using namespace std;
using namespace lumidb;

std::vector<std::string_view> lumidb::split(std::string_view str,
                                            std::string_view delim) {
  std::vector<std::string_view> result;
  size_t pos = 0;
  while ((pos = str.find(delim)) != std::string_view::npos) {
    result.push_back(str.substr(0, pos));
    str.remove_prefix(pos + delim.length());
  }
  if (str.length() > 0) {
    result.push_back(str);
  }
  return result;
}

std::string_view lumidb::trim(std::string_view str) {
  size_t start = 0;
  while (start < str.length() && isspace(str[start])) {
    start++;
  }
  size_t end = str.length() - 1;
  while (end > start && isspace(str[end])) {
    end--;
  }
  return str.substr(start, end - start + 1);
}

Result<CSVObject> lumidb::parse_csv(std::istream &is, std::string_view delim) {
  std::string line;
  if (!std::getline(is, line)) {
    return Error("empty file");
  }

  CSVObject obj;

  auto headers = split(line, delim);

  for (auto &header : headers) {
    obj.headers.push_back(std::string(trim(header)));
  }

  int line_no = 0;
  while (std::getline(is, line)) {
    line_no++;

    auto fields = split(line, delim);
    if (fields.size() != obj.headers.size()) {
      return Error(
          "row size not matched with headers, line={}, expected={}, got={}",
          line_no, obj.headers.size(), fields.size());
    }

    CSVObject::Row row;
    for (auto &field : fields) {
      auto field_val = trim(field);
      row.emplace_back(field_val);
    }

    obj.rows.emplace_back(std::move(row));
  }

  return obj;
}

