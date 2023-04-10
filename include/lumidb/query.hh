#pragma once

#include <ostream>
#include <stdexcept>
#include <vector>

#include "types.hh"

// Query Language

// <func>(<arg1>, <arg2>, ...) | <func>(<arg1>, <arg2>, ...) | ...

namespace lumidb {

struct SourceLocation {
  size_t column_start = 0;
  size_t column_end = 0;

  bool operator==(const SourceLocation &other) const {
    return column_start == other.column_start && column_end == other.column_end;
  }

  friend std::ostream &operator<<(std::ostream &os, const SourceLocation &loc) {
    return os << fmt::format("{}:{}", loc.column_start, loc.column_end);
  }
};

enum class QueryTokenKind {
  Identifier = 0,
  StringLiteral,
  FloatLiteral,
  L_Paren,
  R_Paren,
  Comma,
  Pipe,
  EOS,
  // ErrorToken will ignore the error token and continue to parse the next
  // used for syntax highlighting
  ErrorToken,
};

struct QueryToken {
  SourceLocation loc;
  QueryTokenKind kind;
  AnyValue value;

  bool operator==(const QueryToken &other) const {
    return kind == other.kind && value == other.value;
  }
};

using QueryTokenList = std::vector<QueryToken>;

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
  Query() = default;
  Query(std::vector<QueryFunction> functions) : functions(functions) {}

  std::vector<QueryFunction> functions;

  bool operator==(const Query &other) const {
    if (functions.size() != other.functions.size()) return false;
    for (size_t i = 0; i < functions.size(); i++) {
      if (functions[i] != other.functions[i]) return false;
    }
    return true;
  }

  bool operator!=(const Query &other) const { return !(*this == other); }
};

std::ostream &operator<<(std::ostream &os, const Query &obj);

// tokenize query string into a list of tokens. used in parser and syntax
// highlighter (in REPL).
QueryTokenList tokenize_query(std::string_view query);

// Parse a query string into a Query object.
Result<Query> parse_query(std::string_view query);

}  // namespace lumidb

std::ostream &operator<<(std::ostream &os, const lumidb::QueryToken &obj);

std::ostream &operator<<(std::ostream &os, const lumidb::QueryTokenKind &obj);

template <>
struct fmt::formatter<lumidb::SourceLocation> : fmt::ostream_formatter {};

template <>
struct fmt::formatter<lumidb::QueryToken> : fmt::ostream_formatter {};

template <>
struct fmt::formatter<lumidb::QueryTokenKind> : fmt::ostream_formatter {};

template <>
struct fmt::formatter<lumidb::QueryFunction> : fmt::ostream_formatter {};

template <>
struct fmt::formatter<lumidb::Query> : fmt::ostream_formatter {};
