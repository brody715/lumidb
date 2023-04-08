#include "lumidb/utils.hh"
#include <cctype>

using namespace std;

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
