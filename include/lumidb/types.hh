#pragma once

#include <iomanip>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

namespace lumidb {

using plugin_id_t = int;

// TypeKind
enum TypeKind {
  T_NULL,
  T_INT,
  T_STRING,
};

enum class Status {
  OK = 0,
  ERROR,
  NOT_IMPLEMENTED,
};

std::string status_to_string(Status status);

struct Error {
  Error(Status status, std::string message)
      : status(status), message(message) {}

  explicit Error(Status status)
      : status(status), message(status_to_string(status)) {}

  explicit Error(std::string message)
      : status(Status::ERROR), message(message) {}

  template <typename... T>
  explicit Error(fmt::format_string<T...> fmt, T &&...args)
      : status(Status::ERROR), message(fmt::format(fmt, args...)) {}

  Error add_message(std::string message) const {
    if (this->status == Status::OK) {
      return Error(Status::OK);
    }
    return Error(fmt::format("{}: {}", this->message, message));
  }

  Status status = Status::OK;
  std::string message = "OK";
};

template <typename T>
class Result {
 public:
  Result() = delete;

  Result(Result &&result)
      : error_(std::move(result.error_)), value_(std::move(result.value_)) {}

  Result &operator=(Result &&result) {
    error_ = std::move(result.error_);
    value_ = std::move(result.value_);
    return *this;
  }

  Result(Error error) : error_(error) {}

  Result(T data) : error_(Status::OK), value_(std::move(data)) {}

  T *operator->() { return &value_.value(); }

  const T *operator->() const { return &value_.value(); }

  bool has_error() const { return error_.status != Status::OK; }

  bool is_ok() const { return error_.status == Status::OK; }

  const Error &error() const { return error_; }

  Status status() const { return error_.status; }

  const std::string &message() const { return error_.message; }

  const T &unwrap() const { return value_.value(); }

  T &unwrap() { return value_.value(); }

 private:
  Error error_;
  std::optional<T> value_;
};

class AnyType {
 public:
  AnyType(TypeKind kind) : kind_(kind) {}

  TypeKind kind() const { return kind_; }

  static Result<AnyType> parse_string(std::string str) {
    if (str == "int")
      return AnyType(T_INT);
    else if (str == "string")
      return AnyType(T_STRING);
    else if (str == "null")
      return AnyType(T_NULL);
    else
      return Error("Unknown type: " + str);
  }

  std::string to_string() {
    switch (kind_) {
      case T_INT:
        return "int";
      case T_STRING:
        return "string";
      case T_NULL:
        return "null";
    }
  };

  bool is_null() const { return kind_ == T_NULL; }

 private:
  TypeKind kind_ = T_NULL;
};

// Immutable value type
class AnyValue {
 public:
  AnyValue() : kind_(T_NULL) {}

  AnyValue(int value) : kind_(T_INT), value_(value) {}

  AnyValue(std::string value) : kind_(T_STRING), value_(value) {}

  AnyValue(std::string_view value)
      : kind_(T_STRING), value_(std::string(value)) {}

  TypeKind kind() const { return kind_; }

  static AnyValue from_string(std::string str) { return AnyValue(str); }

  static AnyValue from_int(int value) { return AnyValue(value); }

  static AnyValue from_null() { return AnyValue(); }

  bool operator==(const AnyValue &other) const {
    if (kind_ != other.kind_) return false;
    switch (kind_) {
      case T_INT:
        return as_int() == other.as_int();
      case T_STRING:
        return as_string() == other.as_string();
      case T_NULL:
        return true;
    }
  }

  bool operator!=(const AnyValue &other) const { return !(*this == other); }

  bool is_null() const { return kind_ == T_NULL; }

  int as_int() const { return std::get<int>(value_); }
  const std::string &as_string() const { return std::get<std::string>(value_); }

 private:
  TypeKind kind_;
  std::variant<int, std::string> value_;
};

std::ostream &operator<<(std::ostream &os, const AnyValue &value);

// Query Language

// <func>(<arg1>, <arg2>, ...) | <func>(<arg1>, <arg2>, ...) | ...

struct QueryFunction {
  std::string name;
  std::vector<AnyValue> arguments;

  bool operator==(const QueryFunction &other) const {
    if (name != other.name) return false;
    if (arguments.size() != other.arguments.size()) return false;
    for (size_t i = 0; i < arguments.size(); i++) {
      if (arguments[i] != other.arguments[i]) return false;
    }
    return true;
  }

  bool operator!=(const QueryFunction &other) const {
    return !(*this == other);
  }
};

std::ostream &operator<<(std::ostream &os, const QueryFunction &obj);

struct Query {
  std::vector<QueryFunction> functions;

  bool operator==(const Query &other) const {
    if (functions.size() != other.functions.size()) return false;
    for (size_t i = 0; i < functions.size(); i++) {
      if (functions[i] != other.functions[i]) return false;
    }
    return true;
  }

  bool operator!=(const Query &other) const { return !(*this == other); }

  std::string to_string() const {
    return fmt::format("{}", fmt::join(functions, " | "));
  }
};

// Parse a query string into a Query object.
Result<Query> parse_query(std::string_view query);

}  // namespace lumidb

template <>
struct ::fmt::formatter<lumidb::AnyValue> : ::fmt::ostream_formatter {};

template <>
struct ::fmt::formatter<lumidb::QueryFunction> : ::fmt::ostream_formatter {};