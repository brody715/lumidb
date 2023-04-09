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
enum class TypeKind {
  T_NULL = 0,
  T_ANY,
  T_INT,
  T_STRING,
  T_NULL_INT,
  T_NULL_STRING,
};

enum class ValueTypeKind : int {
  T_NULL = 0,
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

  template <typename... T>
  Error add_message(fmt::format_string<T...> fmt, T &&...args) const {
    if (this->status == Status::OK) {
      return Error(Status::OK);
    }

    std::string message = fmt::format(fmt, args...);
    return Error(fmt::format("{}: {}", message, this->message));
  }

  std::string to_string() const {
    if (status == Status::OK) {
      return "OK";
    }
    return fmt::format("Error: {}", message);
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

  const Error &unwrap_err() const { return error_; }

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
  TypeKind kind() const { return kind_; }

  static AnyType from_value_type(ValueTypeKind kind) {
    switch (kind) {
      case ValueTypeKind::T_NULL:
        return AnyType(TypeKind::T_NULL);
      case ValueTypeKind::T_INT:
        return AnyType(TypeKind::T_INT);
      case ValueTypeKind::T_STRING:
        return AnyType(TypeKind::T_STRING);
      default:
        return AnyType(TypeKind::T_ANY);
    }
  }

  static AnyType from_string() { return AnyType(TypeKind::T_STRING); }
  static AnyType from_int() { return AnyType(TypeKind::T_INT); }
  static AnyType from_null() { return AnyType(TypeKind::T_NULL); }
  static AnyType from_any() { return AnyType(TypeKind::T_ANY); }

  bool is_null() const { return kind_ == TypeKind::T_NULL; }
  bool is_string() const { return kind_ == TypeKind::T_STRING; }
  bool is_int() const { return kind_ == TypeKind::T_INT; }
  bool is_any() const { return kind_ == TypeKind::T_ANY; }
  bool is_null_string() const { return kind_ == TypeKind::T_NULL_STRING; }
  bool is_null_int() const { return kind_ == TypeKind::T_NULL_INT; }

  bool is_subtype_of(const AnyType &other) const {
    switch (other.kind_) {
      case TypeKind::T_ANY:
        return true;
      case TypeKind::T_NULL_INT:
        return kind_ == TypeKind::T_INT || kind_ == TypeKind::T_NULL_INT ||
               kind_ == TypeKind::T_NULL;
      case TypeKind::T_NULL_STRING:
        return kind_ == TypeKind::T_STRING ||
               kind_ == TypeKind::T_NULL_STRING || kind_ == TypeKind::T_NULL;
      default:
        return kind_ == other.kind_;
    }
  }

  static Result<AnyType> parse_string(std::string str) {
    if (str == "int")
      return AnyType(TypeKind::T_INT);
    else if (str == "string")
      return AnyType(TypeKind::T_STRING);
    else if (str == "int?")
      return AnyType(TypeKind::T_NULL_INT);
    else if (str == "string?")
      return AnyType(TypeKind::T_NULL_STRING);
    else if (str == "null")
      return AnyType(TypeKind::T_NULL);
    else if (str == "any")
      return AnyType(TypeKind::T_ANY);
    else
      return Error("Unknown type: " + str);
  }

  std::string name() const {
    switch (kind_) {
      case TypeKind::T_INT:
        return "int";
      case TypeKind::T_STRING:
        return "string";
      case TypeKind::T_ANY:
        return "any";
      case TypeKind::T_NULL:
        return "null";
      case TypeKind::T_NULL_INT:
        return "int?";
      case TypeKind::T_NULL_STRING:
        return "string?";
      default:
        return "unknown";
    }
  };

 private:
  AnyType(TypeKind kind) : kind_(kind) {}

 private:
  TypeKind kind_ = TypeKind::T_NULL;
};

// Immutable value type
class AnyValue {
 public:
  AnyValue(std::string_view str)
      : AnyValue(ValueTypeKind::T_STRING, std::string(str)) {}
  AnyValue(std::string str) : AnyValue(ValueTypeKind::T_STRING, str) {}
  AnyValue(int value) : AnyValue(ValueTypeKind::T_INT, value) {}
  AnyValue() : AnyValue(ValueTypeKind::T_NULL, {}) {}

  ValueTypeKind kind() const { return kind_; }
  AnyType type() const { return type_; }

  static AnyValue from_string(std::string_view str) {
    return AnyValue(ValueTypeKind::T_STRING, std::string(str));
  }
  static AnyValue from_string(std::string str) {
    return AnyValue(ValueTypeKind::T_STRING, str);
  }

  static AnyValue from_int(int value) {
    return AnyValue(ValueTypeKind::T_INT, value);
  }

  static AnyValue from_null() { return AnyValue(ValueTypeKind::T_NULL, 0); }

  bool operator==(const AnyValue &other) const {
    if (kind_ != other.kind_) return false;
    switch (kind_) {
      case ValueTypeKind::T_INT:
        return as_int() == other.as_int();
      case ValueTypeKind::T_STRING:
        return as_string() == other.as_string();
      case ValueTypeKind::T_NULL:
        return true;
    }
  }

  bool operator!=(const AnyValue &other) const { return !(*this == other); }

  bool is_instance_of(const AnyType &type) const {
    return type.is_subtype_of(this->type());
  }

  bool is_null() const { return kind_ == ValueTypeKind::T_NULL; }
  bool is_string() const { return kind_ == ValueTypeKind::T_STRING; }
  bool is_int() const { return kind_ == ValueTypeKind::T_INT; }

  int as_int() const { return std::get<int>(value_); }
  const std::string &as_string() const { return std::get<std::string>(value_); }

  std::string format_to_string() const;

 private:
  AnyValue(ValueTypeKind kind, std::variant<int, std::string> value)
      : kind_(kind), value_(value), type_(AnyType::from_value_type(kind)) {}

 private:
  ValueTypeKind kind_;
  std::variant<int, std::string> value_;
  AnyType type_;
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