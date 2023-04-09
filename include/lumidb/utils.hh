#pragma once

#include <string_view>
#include <vector>

namespace lumidb {
std::vector<std::string_view> split(std::string_view str,
                                    std::string_view delim);

std::string_view trim(std::string_view str);

class IdGenerator {
 public:
  int next_id() { return next_id_++; }

 private:
  int next_id_ = 1;
};

}  // namespace lumidb