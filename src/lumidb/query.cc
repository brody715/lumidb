#include "lumidb/query.hh"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <exception>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"

using namespace lumidb;
using namespace std;

// query token helpers

template <typename Predict>
QueryTokenList filter_query_token_list(const QueryTokenList src_datas,
                                       const Predict &predict) {
  QueryTokenList result;
  for (auto &data : src_datas) {
    if (predict(data)) {
      result.emplace_back(data);
    }
  }
  return result;
}

std::ostream &operator<<(std::ostream &os, const QueryTokenKind &kind) {
  switch (kind) {
    case QueryTokenKind::Identifier:
      os << "Identifier";
      break;
    case QueryTokenKind::StringLiteral:
      os << "StringLiteral";
      break;
    case QueryTokenKind::FloatLiteral:
      os << "FloatLiteral";
      break;
    case QueryTokenKind::L_Paren:
      os << "L_Paren";
      break;
    case QueryTokenKind::R_Paren:
      os << "R_Parent";
      break;
    case QueryTokenKind::Comma:
      os << "Comma";
      break;
    case QueryTokenKind::Pipe:
      os << "Pipe";
      break;
    case QueryTokenKind::EOS:
      os << "EOS";
      break;
    case QueryTokenKind::ErrorToken:
      os << "UnknownChar";
      break;
    default:
      throw std::runtime_error("unknown token kind");
  }
  return os;
}

std::ostream &operator<<(std::ostream &os, const QueryToken &obj) {
  if (obj.value.is_null()) {
    os << fmt::format("{{loc={}, kind={}}}", obj.loc, obj.kind);
  } else {
    os << fmt::format("{{loc={}, kind={}, value={}}}", obj.loc, obj.kind,
                      obj.value);
  }
  return os;
}

class QueryLexer {
 public:
  QueryLexer(string_view content) : content_(content) {}

  QueryToken next_token() {
    while (true) {
      if (content_.empty()) {
        auto loc = step_location(0);
        return QueryToken{loc, QueryTokenKind::EOS};
      }

      while (isspace(content_[0])) {
        step_location(1);
      }

      if (content_[0] == '(') {
        auto loc = step_location(1);
        return QueryToken{loc, QueryTokenKind::L_Paren, {}};
      }

      if (content_[0] == ')') {
        auto loc = step_location(1);
        return QueryToken{loc, QueryTokenKind::R_Paren, {}};
      }

      if (content_[0] == ',') {
        auto loc = step_location(1);
        return QueryToken{loc, QueryTokenKind::Comma, {}};
      }

      if (content_[0] == '|') {
        auto loc = step_location(1);
        return QueryToken{loc, QueryTokenKind::Pipe};
      }

      if (content_[0] == '"' || content_[0] == '\'') {
        auto quote = content_[0];
        auto [value, size] = parse_string_literal(content_, quote);
        if (size == string_view::npos) {
          auto loc = step_location(content_.length());
          return QueryToken{loc, QueryTokenKind::ErrorToken, value};
        }

        auto loc = step_location(size);
        return QueryToken{loc, QueryTokenKind::StringLiteral, value};
      }

      if (isdigit(content_[0]) || content_[0] == '-') {
        return parse_float(content_);
      }

      // try identifier
      if (auto token = parse_identifier(content_); token) {
        return token.value();
      }

      // unknown char
      if (content_[0] != ' ') {
        auto ch = std::string(content_[0], 1);
        auto loc = step_location(1);
        return QueryToken{loc, QueryTokenKind::ErrorToken, ch};
      }
    }
  }

 private:
  QueryToken parse_float(std::string_view input) {
    auto end = input.find_first_not_of("0123456789.");
    if (end == string_view::npos) {
      end = input.length();
    }
    auto value = input.substr(0, end);

    try {
      float val = std::stod(std::string(value));

      auto loc = step_location(value.length());
      return QueryToken{loc, QueryTokenKind::FloatLiteral,
                        AnyValue::from_float(val)};
    } catch (const std::invalid_argument &e) {
      auto loc = step_location(value.length());
      return QueryToken{loc, QueryTokenKind::ErrorToken, value};
    }
  }

  std::optional<QueryToken> parse_identifier(std::string_view input) {
    size_t end = 0;
    for (; end < input.length(); end++) {
      // 0-9, a-z, A-Z, _
      if (!isalnum(input[end]) && input[end] != '_') {
        break;
      }
    }

    if (end == 0) {
      return std::nullopt;
    }

    auto value = input.substr(0, end);
    auto loc = step_location(value.length());
    return QueryToken{loc, QueryTokenKind::Identifier,
                      AnyValue::from_string(value)};
  }

  // return {value, comsume_size} because we may encounter a escaped char,
  // consume 2 chars
  std::pair<std::string, size_t> parse_string_literal(std::string_view input,
                                                      char quote = '"') {
    assert(input[0] == quote);

    std::string result = "";
    size_t i = 1;

    while (i < input.length()) {
      if (input[i] == quote) {
        return {result, i + 1};
      }

      if (input[i] == '\\') {
        if (i + 1 >= input.length()) {
          return {result, std::string::npos};
        }

        switch (input[i + 1]) {
          case 'a':
            result += '\a';
            break;
          case 'n':
            result += '\n';
            break;
          case 'r':
            result += '\r';
            break;
          case 't':
            result += '\t';
            break;
          case 'b':
            result += '\b';
            break;
          // includes '"" or '\'', ...
          default:
            result += input[i + 1];
            break;
        }
        i += 2;
        continue;
      }

      result += input[i];
      i += 1;
    }

    return {result, std::string::npos};
  }

  SourceLocation step_location(size_t n) {
    if (n >= content_.length()) {
      n = content_.length();
    }

    auto loc = SourceLocation{
        .column_start = column_start_,
        .column_end = column_start_ + n,
    };

    content_ = content_.substr(n);
    column_start_ += n;

    return loc;
  }

 private:
  string_view content_;
  size_t column_start_ = 0;
};

class ParseException {
 public:
  ParseException(SourceLocation loc, std::string msg) : loc_(loc), msg_(msg) {}

  Error to_error() const { return Error("parse error at {}: {}", loc_, msg_); }

 private:
  SourceLocation loc_;
  std::string msg_;
};

class QueryParser {
 public:
  QueryParser(QueryTokenList tokens) : tokens_(tokens) {}

  Result<Query> parse() {
    if (tokens_.empty()) {
      throw ParseException(SourceLocation{}, "empty query");
    }

    // check error token first
    for (auto &token : tokens_) {
      if (token.kind == QueryTokenKind::ErrorToken) {
        throw ParseException(token.loc, "invalid token");
      }
    }

    try {
      return parse_query();
    } catch (const ParseException &e) {
      return e.to_error();
    }
  }

 private:
  // parse <func> | <func>
  Query parse_query() {
    Query query;

    std::vector<QueryFunction> functions;
    while (true) {
      auto func = parse_query_function();
      functions.push_back(func);

      auto token = expect({QueryTokenKind::Pipe, QueryTokenKind::EOS});

      if (token.kind == QueryTokenKind::EOS) {
        break;
      }
    }

    return {functions};
  }

  // parse <func>(<args>, <args>, ...)
  // like `sum(1, 2, 3)`, `select("hello", "world")`
  // allow empty args like <func> | <func>
  QueryFunction parse_query_function() {
    auto token = expect(QueryTokenKind::Identifier);
    auto func_name = token.value.as_string();
    token = expect(
        {QueryTokenKind::L_Paren, QueryTokenKind::EOS, QueryTokenKind::Pipe});

    if (token.kind == QueryTokenKind::EOS ||
        token.kind == QueryTokenKind::Pipe) {
      return QueryFunction{func_name, {}};
    }

    // if current is rparen, return
    if (peek().kind == QueryTokenKind::R_Paren) {
      next_token();
      return QueryFunction{func_name, {}};
    }

    std::vector<AnyValue> args;
    while (true) {
      args.push_back(parse_value());

      token = expect({QueryTokenKind::R_Paren, QueryTokenKind::Comma});

      if (token.kind == QueryTokenKind::R_Paren) {
        break;
      }
    }

    return QueryFunction{func_name, args};
  }

  AnyValue parse_value() {
    auto token = next_token();
    switch (token.kind) {
      case QueryTokenKind::StringLiteral:
      case QueryTokenKind::FloatLiteral:
      case QueryTokenKind::Identifier:
        return token.value;
      default:
        throw ParseException(
            token.loc, fmt::format("unexpected token, expected: value, got: {}",
                                   token.kind));
    }
  }

  QueryToken peek(size_t n = 0) {
    if (index_ >= tokens_.size()) {
      return QueryToken{SourceLocation{}, QueryTokenKind::EOS};
    }

    return tokens_[index_ + n];
  }

  QueryToken next_token() {
    auto token = peek();
    index_ += 1;
    return token;
  }

  QueryToken expect(const vector<QueryTokenKind> &kinds) {
    auto token = next_token();
    for (auto kind : kinds) {
      if (token.kind == kind) {
        return token;
      }
    }

    throw ParseException(token.loc,
                         fmt::format("unexpected token, expected: {}, got: {}",
                                     fmt::join(kinds, ", "), token.kind));
  }

  QueryToken expect(QueryTokenKind kind) {
    auto token = next_token();
    if (token.kind != kind) {
      throw ParseException(
          token.loc, fmt::format("unexpected token, expected: {}, got: {}",
                                 kind, token.kind));
    }

    return token;
  }

 private:
  QueryTokenList tokens_;
  size_t index_ = 0;
};

std::ostream &lumidb::operator<<(std::ostream &os, const QueryFunction &obj) {
  os << fmt::format("{}({})", obj.name, fmt::join(obj.arguments, ", "));
  return os;
}

std::ostream &lumidb::operator<<(std::ostream &os, const Query &func) {
  return os << fmt::format("{}", fmt::join(func.functions, " | "));
}

QueryTokenList lumidb::tokenize_query(std::string_view query) {
  QueryTokenList tokens;
  QueryLexer lexer(query);

  while (true) {
    auto token = lexer.next_token();
    if (token.kind == QueryTokenKind::EOS) {
      break;
    }
    tokens.push_back(token);
  }

  return tokens;
}

Result<Query> lumidb::parse_query(std::string_view query) {
  auto tokens = tokenize_query(query);

  QueryParser parser(tokens);
  return parser.parse();
}