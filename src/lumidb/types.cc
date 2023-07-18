#include "lumidb/types.hh"

#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "fmt/core.h"
#include "lumidb/utils.hh"
using namespace std;
using namespace lumidb;

int lumidb::compare_float(float a, float b, float epsilon) {
  float diff = a - b;
  if (diff < -epsilon) {
    return -1;
  } else if (diff > epsilon) {
    return 1;
  } else {
    return 0;
  }
}

std::string lumidb::float2string(float v) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%.2f", v);
  size_t len = strlen(buf);
  while (buf[len - 1] == '0') {
    len--;
  }
  if (buf[len - 1] == '.') {
    len--;
  }

  return std::string(buf, len);
}

std::string lumidb::status_to_string(Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::Error:
      return "ERROR";
    case Status::NOT_IMPLEMENTED:
      return "NOT_IMPLEMENTED";
  }
  return "UNKNOWN";
}

std::ostream &lumidb::operator<<(std::ostream &os, const AnyValue &value) {
  switch (value.kind()) {
    case ValueTypeKind::T_FLOAT:
      os << float2string(value.as_float());
      break;
    case ValueTypeKind::T_STRING:
      os << std::quoted(value.as_string(), '\'');
      break;
    case ValueTypeKind::T_NULL:
      os << "null";
      break;
    default:
      os << "unsupported type=" << static_cast<int>(value.kind());
  }
  return os;
}

std::string AnyValue::format_to_string() const {
  thread_local static std::ostringstream os{};
  os.str("");
  os << *this;
  return os.str();
}

Result<AnyValue> AnyValue::parse_from_string(const AnyType &type,
                                             std::string_view str) {
  switch (type.kind()) {
    case TypeKind::T_FLOAT:
    case TypeKind::T_NULL_FLOAT:
      if (type.is_null_float() && (str == "" || str == "null")) {
        return AnyValue::from_null();
      }

      try {
        return AnyValue::from_float(std::stod(std::string(str)));
      } catch (const std::exception &e) {
        return Error("invalid number: {}", str);
      }
    case TypeKind::T_STRING:
    case TypeKind::T_NULL_STRING:
      if (type.is_null() && str == "null") {
        return AnyValue::from_null();
      }
      if (str.empty()) {
        return AnyValue::from_string(str);
      }

      if (str[0] == '"' && str[str.length() - 1] == '"') {
        return AnyValue::from_string(str.substr(1, str.length() - 2));
      }

      if (str[0] == '\'' && str[str.length() - 1] == '\'') {
        return AnyValue::from_string(str.substr(1, str.length() - 2));
      }

      return AnyValue::from_string(str);
    case TypeKind::T_NULL:
      return AnyValue::from_null();
    case TypeKind::T_ANY:
      return AnyValue::from_string(str);
    default:
      return Error("unsupported type: {}", type.name());
  }
}

Result<AnyValue::Comparator> AnyValue::get_comparator(std::string op) {
  CompareOperator compare_op;

  if (op == "=") {
    compare_op = CompareOperator::EQ;
  } else if (op == "<") {
    compare_op = CompareOperator::LT;
  } else if (op == ">") {
    compare_op = CompareOperator::GT;
  } else {
    return Error("unsupported operator: {}", op);
  }

  return AnyValue::get_comparator(compare_op);
}

AnyValue::Comparator AnyValue::get_comparator(CompareOperator op) {
  switch (op) {
    case CompareOperator::EQ:
      return
          [](const AnyValue &lhs, const AnyValue &rhs) { return lhs == rhs; };
    case CompareOperator::LT:
      return [](const AnyValue &lhs, const AnyValue &rhs) { return lhs < rhs; };
    case CompareOperator::GT:
      return [](const AnyValue &lhs, const AnyValue &rhs) { return lhs > rhs; };
      break;
  }
  throw std::runtime_error("unsupported operator");
}
