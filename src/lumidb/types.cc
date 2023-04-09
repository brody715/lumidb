#include "lumidb/types.hh"

#include <string_view>

#include "lumidb/utils.hh"
using namespace std;
using namespace lumidb;

std::string lumidb::status_to_string(Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::ERROR:
      return "ERROR";
    case Status::NOT_IMPLEMENTED:
      return "NOT_IMPLEMENTED";
  }
  return "UNKNOWN";
}

std::ostream &lumidb::operator<<(std::ostream &os, const QueryFunction &obj) {
  os << fmt::format("{}({})", obj.name, fmt::join(obj.arguments, ", "));
  return os;
}

std::ostream &lumidb::operator<<(std::ostream &os, const AnyValue &value) {
  switch (value.kind()) {
    case ValueTypeKind::T_INT:
      os << std::to_string(value.as_int());
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
  thread_local static std::ostringstream os;
  os.str("");
  os << *this;
  return os.str();
}

Result<AnyValue> parse_value(string_view input) {
  if (input.empty()) {
    return Error("empty value");
  }

  if (input[0] == '"' && input[input.length() - 1] == '"') {
    return AnyValue::from_string(input.substr(1, input.length() - 2));
  }

  if (input[0] == '\'' && input[input.length() - 1] == '\'') {
    return AnyValue::from_string(input.substr(1, input.length() - 2));
  }

  if (input == "null") {
    return AnyValue::from_null();
  }

  // parse number
  if (isdigit(input[0]) || input[0] == '-') {
    try {
      return AnyValue::from_int(std::stoi(std::string(input)));
    } catch (const std::exception &e) {
      return Error("invalid number: {}", input);
    }
  }

  return Error("invalid value: {}", input);
}

// parse <func>(<args>, <args>, ...)
// like `sum(1, 2, 3)`, `select("hello", "world")`
Result<QueryFunction> parse_query_functions(string_view input) {
  // split the input string

  // parse the function name

  auto first_lp = input.find('(');
  auto last_rp = input.rfind(')');

  // no parentheses, just a function name
  if (first_lp == string_view::npos && last_rp == string_view::npos) {
    return QueryFunction{.name = std::string(input)};
  }

  if (first_lp == string_view::npos || last_rp == string_view::npos) {
    return Error("invalid function format, parentheses not match");
  }

  auto arg_str = trim(input.substr(first_lp + 1, last_rp - first_lp - 1));

  QueryFunction func;
  func.name = trim(input.substr(0, first_lp));

  // parse the function arguments
  auto args = split(arg_str, ",");
  func.arguments.reserve(args.size());

  for (auto &arg : args) {
    auto arg_trim = trim(arg);
    auto value = parse_value(arg_trim);

    if (value.has_error()) {
      return value.unwrap_err().add_message(
          fmt::format("in function {}", func.name));
    }

    func.arguments.push_back(value.unwrap());
  }

  return func;
}

Result<Query> lumidb::parse_query(string_view input) {
  // split the input string
  auto parts = split(input, "|");

  Query query;
  query.functions.reserve(parts.size());

  for (auto &part : parts) {
    auto func = parse_query_functions(trim(part));
    if (func.has_error()) {
      return func.unwrap_err().add_message("in query: part=" +
                                           std::string(part));
    }
    query.functions.push_back(func.unwrap());
  }

  if (query.functions.empty()) {
    return Error("empty query");
  }

  return query;
}
