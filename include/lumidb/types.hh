#pragma once

#include <functional>
#include <iomanip>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "fmt/core.h"
#include "fmt/ostream.h"
#include "fmt/ranges.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#define LUMIDB_PLATFORM_WINDOWS 1
#elif __linux__
#define LUMIDB_PLATFORM_LINUX 1
#elif __APPLE__
#define LUMIDB_PLATFORM_MACOS 1
#else
#endif

namespace lumidb {

class Logger {
 public:
  enum LogLevel {
    Normal = 0,
    Error,
    Warning,
    Info,
    Debug,
  };
  virtual void log(LogLevel level, const std::string &message) = 0;
};

using LoggerPtr = std::shared_ptr<Logger>;

int compare_float(float a, float b, float epsilon = 0.0001);
std::string float2string(float v);

using plugin_id_t = std::string;

// TypeKind
enum class TypeKind {
  T_NULL = 0,
  T_ANY,
  T_FLOAT,
  T_STRING,
  T_NULL_FLOAT,
  T_NULL_STRING,
};

enum class ValueTypeKind : int {
  T_NULL = 0,
  T_FLOAT,
  T_STRING,
};

enum class CompareOperator {
  EQ = 0,
  LT,
  GT,
};

enum class Status {
  OK = 0,
  Error,
  NOT_IMPLEMENTED,
};

std::string status_to_string(Status status);

struct Error {
  Error(Status status, std::string message)
      : status(status), message(message) {}

  explicit Error(Status status)
      : status(status), message(status_to_string(status)) {}

  explicit Error(std::string message)
      : status(Status::Error), message(message) {}

  template <typename... T>
  explicit Error(fmt::format_string<T...> fmt, T &&...args)
      : status(Status::Error), message(fmt::format(fmt, args...)) {}

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
    return message;
  }

  Status status = Status::OK;
  std::string message = "OK";
};

template <typename T>
class Result {
 public:
  Result() = delete;

  Result(const Result &result) : error_(result.error_), value_(result.value_){};

  Result(Result &&result)
      : error_(std::move(result.error_)), value_(std::move(result.value_)) {}

  Result &operator=(Result &&result) {
    error_ = std::move(result.error_);
    value_ = std::move(result.value_);
    return *this;
  }

  Result &operator=(const Result &result) {
    error_ = result.error_;
    value_ = result.value_;
    return *this;
  }

  Result(Error error) : error_(error) {}

  Result(T data) : error_(Status::OK), value_(std::move(data)) {}

  operator bool() const { return error_.status == Status::OK; }

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
      case ValueTypeKind::T_FLOAT:
        return AnyType(TypeKind::T_FLOAT);
      case ValueTypeKind::T_STRING:
        return AnyType(TypeKind::T_STRING);
      default:
        return AnyType(TypeKind::T_ANY);
    }
  }

  static AnyType from_null_string() { return AnyType(TypeKind::T_NULL_STRING); }
  static AnyType from_null_float() { return AnyType(TypeKind::T_NULL_FLOAT); }
  static AnyType from_string() { return AnyType(TypeKind::T_STRING); }
  static AnyType from_float() { return AnyType(TypeKind::T_FLOAT); }
  static AnyType from_null() { return AnyType(TypeKind::T_NULL); }
  static AnyType from_any() { return AnyType(TypeKind::T_ANY); }

  bool is_null() const { return kind_ == TypeKind::T_NULL; }
  bool is_string() const { return kind_ == TypeKind::T_STRING; }
  bool is_float() const { return kind_ == TypeKind::T_FLOAT; }
  bool is_any() const { return kind_ == TypeKind::T_ANY; }
  bool is_null_string() const { return kind_ == TypeKind::T_NULL_STRING; }
  bool is_null_float() const { return kind_ == TypeKind::T_NULL_FLOAT; }

  bool is_subtype_of(const AnyType &other) const {
    switch (other.kind_) {
      case TypeKind::T_ANY:
        return true;
      case TypeKind::T_NULL_FLOAT:
        return kind_ == TypeKind::T_FLOAT || kind_ == TypeKind::T_NULL_FLOAT ||
               kind_ == TypeKind::T_NULL;
      case TypeKind::T_NULL_STRING:
        return kind_ == TypeKind::T_STRING ||
               kind_ == TypeKind::T_NULL_STRING || kind_ == TypeKind::T_NULL;
      default:
        return kind_ == other.kind_;
    }
  }

  static Result<AnyType> parse_string(std::string str) {
    if (str == "float")
      return AnyType(TypeKind::T_FLOAT);
    else if (str == "string")
      return AnyType(TypeKind::T_STRING);
    else if (str == "float?")
      return AnyType(TypeKind::T_NULL_FLOAT);
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
      case TypeKind::T_FLOAT:
        return "float";
      case TypeKind::T_STRING:
        return "string";
      case TypeKind::T_ANY:
        return "any";
      case TypeKind::T_NULL:
        return "null";
      case TypeKind::T_NULL_FLOAT:
        return "float?";
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
  using Comparator =
      std::function<bool(const AnyValue &lhs, const AnyValue &rhs)>;

  AnyValue() : AnyValue(ValueTypeKind::T_NULL, {}) {}
  AnyValue(std::string_view str)
      : AnyValue(ValueTypeKind::T_STRING, std::string(str)) {}
  AnyValue(std::string str) : AnyValue(ValueTypeKind::T_STRING, str) {}
  AnyValue(float value) : AnyValue(ValueTypeKind::T_FLOAT, value) {}

  ValueTypeKind kind() const { return kind_; }
  AnyType type() const { return type_; }

  static Result<AnyValue> parse_from_string(const AnyType &type,
                                            std::string_view str);

  static Comparator get_comparator(CompareOperator op);
  static Result<Comparator> get_comparator(std::string op);

  static AnyValue from_string(std::string_view str) {
    return AnyValue(ValueTypeKind::T_STRING, std::string(str));
  }
  static AnyValue from_string(std::string str) {
    return AnyValue(ValueTypeKind::T_STRING, str);
  }
  static AnyValue from_string(const char *str) {
    return AnyValue(ValueTypeKind::T_STRING, std::string(str));
  }

  static AnyValue from_float(float value) {
    return AnyValue(ValueTypeKind::T_FLOAT, value);
  }

  static AnyValue from_null() { return AnyValue(ValueTypeKind::T_NULL, {}); }

  bool operator<(const AnyValue &other) const {
    if (kind_ != other.kind_) return kind_ < other.kind_;
    switch (kind_) {
      case ValueTypeKind::T_FLOAT:
        return as_float() < other.as_float();
      case ValueTypeKind::T_STRING:
        return as_string() < other.as_string();
      case ValueTypeKind::T_NULL:
        return false;
    }

    return false;
  }

  bool operator>(const AnyValue &other) const { return other < *this; }

  bool operator==(const AnyValue &other) const {
    if (kind_ != other.kind_) return false;
    switch (kind_) {
      case ValueTypeKind::T_FLOAT:
        return compare_float(as_float(), other.as_float()) == 0;
      case ValueTypeKind::T_STRING:
        return as_string() == other.as_string();
      case ValueTypeKind::T_NULL:
        return true;
    }
    throw new std::runtime_error("Unknown value type");
  }

  bool operator!=(const AnyValue &other) const { return !(*this == other); }

  bool is_instance_of(const AnyType &type) const {
    return this->type_.is_subtype_of(type);
  }

  bool is_null() const { return kind_ == ValueTypeKind::T_NULL; }
  bool is_string() const { return kind_ == ValueTypeKind::T_STRING; }
  bool is_float() const { return kind_ == ValueTypeKind::T_FLOAT; }

  float as_float() const { return std::get<float>(value_); }
  const std::string &as_string() const { return std::get<std::string>(value_); }

  std::string format_to_string() const;

 private:
  AnyValue(ValueTypeKind kind, std::variant<float, std::string> value)
      : kind_(kind), value_(value), type_(AnyType::from_value_type(kind)) {}

 private:
  ValueTypeKind kind_;
  std::variant<float, std::string> value_;
  AnyType type_;
};

std::ostream &operator<<(std::ostream &os, const AnyValue &value);

}  // namespace lumidb

template <>
struct fmt::formatter<lumidb::AnyValue> : fmt::ostream_formatter {};
