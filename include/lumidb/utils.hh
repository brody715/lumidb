#pragma once

#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "fmt/core.h"
#include "lumidb/types.hh"

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

// We guarantee that every row has the same number of columns, and equal to
// headers.size()
struct CSVObject {
  using Row = std::vector<std::string>;
  std::vector<std::string> headers;
  std::vector<Row> rows;

  bool operator==(const CSVObject &other) const {
    return headers == other.headers && rows == other.rows;
  }

  bool operator!=(const CSVObject &other) const { return !(*this == other); };
};

Result<CSVObject> parse_csv(std::istream &is, std::string_view delim = ",");

// Set which preserves the insertion order
template <typename T>
class InsertOrderSet {
 public:
  using iterator = typename std::vector<T>::iterator;
  using const_iterator = typename std::vector<T>::const_iterator;

  void insert(const T &item) {
    if (set_.find(item) == set_.end()) {
      items_.push_back(item);
      set_.insert(item);
    }
  }

  void insert(T &&item) {
    if (set_.find(item) == set_.end()) {
      items_.push_back(std::move(item));
      set_.insert(item);
    }
  }

  void clear() {
    items_.clear();
    set_.clear();
  }

  bool contains(const T &item) const { return set_.find(item) != set_.end(); }

  size_t size() const { return items_.size(); }

  iterator begin() { return items_.begin(); }
  iterator end() { return items_.end(); }
  const_iterator begin() const { return items_.begin(); }
  const_iterator end() const { return items_.end(); }

  const T &operator[](size_t index) const { return items_[index]; }

  T &operator[](size_t index) { return items_[index]; }

  bool operator==(const InsertOrderSet &other) const {
    return items_ == other.items_;
  }

  bool operator!=(const InsertOrderSet &other) const {
    return !(*this == other);
  }

 private:
  std::vector<T> items_;
  std::unordered_set<T> set_;
};

}  // namespace lumidb