#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "acutest.h"
#include "lumidb/query.hh"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"
#include "testlib.hh"

using namespace std;
using namespace lumidb;

void test_strings_trim() {
  struct TestCase {
    string input;
    string expected;
  };

  vector<TestCase> cases{
      {"   hello, world   ", "hello, world"},
      {"   a", "a"},
      {"b   ", "b"},
      {"c   d   e", "c   d   e"},
  };

  for (auto &c : cases) {
    auto result = lumidb::trim(c.input);
    TEST_CHECK_(result == c.expected, "expected: %s, got: %s",
                c.expected.c_str(), result.data());
  }
}

void test_strings_split() {
  struct TestCase {
    string input;
    string delim;
    vector<string_view> expected;
  };

  vector<TestCase> cases{
      {"hello, world", ",", {"hello", " world"}},
      {"func1(a1, a2) | func2(a3, a4)",
       "|",
       {"func1(a1, a2) ", " func2(a3, a4)"}},
  };

  for (auto &c : cases) {
    auto result = lumidb::split(c.input, c.delim);
    TEST_CHECK_(result == c.expected, "%s",
                fmt::format("input error: {}", c.input).c_str());
  }
}

void test_tokenize_query_kind() {
  using qtk = QueryTokenKind;
  struct TestCase {
    string input;
    vector<QueryTokenKind> expected;
  };

  auto kinds_equal = [](const vector<QueryToken> &lhs,
                        const vector<QueryTokenKind> &rhs) -> bool {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    for (size_t i = 0; i < lhs.size(); i++) {
      if (lhs[i].kind != rhs[i]) {
        return false;
      }
    }

    return true;
  };

  vector<TestCase> cases{
      {"create_table('students', 'good')",
       {qtk::Identifier, qtk::L_Paren, qtk::StringLiteral, qtk::Comma,
        qtk::StringLiteral, qtk::R_Paren}},
      {
          "func1(null, 10, 20, 30, \"hello\")",
          {qtk::Identifier, qtk::L_Paren, qtk::Identifier, qtk::Comma,
           qtk::FloatLiteral, qtk::Comma, qtk::FloatLiteral, qtk::Comma,
           qtk::FloatLiteral, qtk::Comma, qtk::StringLiteral, qtk::R_Paren},
      },
      {"@aaa 'abc' 'aaaa",
       {qtk::ErrorToken, qtk::Identifier, qtk::StringLiteral, qtk::ErrorToken}},
      // escaped
      {"func1('a\\'b', 'a\\' \\tb')",
       {qtk::Identifier, qtk::L_Paren, qtk::StringLiteral, qtk::Comma,
        qtk::StringLiteral, qtk::R_Paren}},
  };

  for (auto &c : cases) {
    auto result = lumidb::tokenize_query(c.input);
    TEST_CHECK_(kinds_equal(result, c.expected), "%s",
                fmt::format("input error: {}", c.input).c_str());
  }
}

void test_tokenize_query_loc() {
  struct TestCase {
    string input;
    vector<SourceLocation> expected;
  };

  auto locs_equal = [](const vector<QueryToken> &lhs,
                       const vector<SourceLocation> &rhs) -> bool {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    for (size_t i = 0; i < lhs.size(); i++) {
      if (!(lhs[i].loc == rhs[i])) {
        return false;
      }
    }

    return true;
  };

  vector<TestCase> cases = {
      {"create_table('students')",
       {/* ident */ {0, 12}, /* lparen */ {12, 13}, /* string */ {13, 23},
        /* rparen */ {23, 24}}},
  };

  for (auto &c : cases) {
    auto result = lumidb::tokenize_query(c.input);
    TEST_CHECK_(
        locs_equal(result, c.expected), "%s",
        fmt::format("input error: {}, output={}", c.input, result).c_str());
  }
}

void test_parse_query() {
  struct TestCase {
    string input;
    bool expected_error;
    string expected;
  };

  vector<TestCase> cases{
      // simple
      {"create_table('students', 'good')", false,
       "create_table('students', 'good')"},
      // mixed
      {"func1(null, 10, 20, 30, \"hello\")", false,
       "func1(null, 10, 20, 30, 'hello')"},
      // pipe
      {"func1(10, 20, 30) | func2(40, 50, 60)", false,
       "func1(10, 20, 30) | func2(40, 50, 60)"},
      // space
      {"func1(10,     20,          'hello world')", false,
       "func1(10, 20, 'hello world')"},
  };

  for (auto &c : cases) {
    auto result = lumidb::parse_query(c.input);
    if (c.expected_error) {
      TEST_CHECK_(result.has_error(), "%s",
                  fmt::format("input error: {}", c.input).c_str());
    } else {
      TEST_CHECK_(result.is_ok(), "%s",
                  fmt::format("input error: {}", c.input).c_str());
      auto got = fmt::format("{}", result.unwrap());
      TEST_CHECK_(
          got == c.expected, "%s",
          fmt::format("input error: exp={}, got={}", c.expected, got).c_str());
    }
  }
}

void test_parse_csv() {
  struct TestCase {
    string input;
    bool expected_error;
    lumidb::CSVObject expected;
  };

  vector<TestCase> cases{
      {"a1,a2,a3\n1,2,3\n4,5,6",
       false,
       {{"a1", "a2", "a3"}, {{"1", "2", "3"}, {"4", "5", "6"}}}},
      {"a1,a2\n1,2\n1,2,3", true, {}},
  };

  for (size_t i = 0; i < cases.size(); i++) {
    TEST_CASE_("%ld", i);
    auto &c = cases[i];
    std::stringstream ss(c.input);
    auto result = lumidb::parse_csv(ss);
    if (c.expected_error) {
      TEST_CHECK_(result.has_error(), "%s",
                  fmt::format("input error: {}", c.input).c_str());
    } else {
      TEST_CHECK_(result.is_ok(), "%s",
                  fmt::format("input error: {}", c.input).c_str());
      auto got = result.unwrap();
      TEST_CHECK_(got == c.expected, "%s",
                  fmt::format("input error: i={}", i).c_str());
    }
  }
}

#ifndef DEBUG_MAIN
TEST_LIST = {TEST_FUNC(test_strings_trim),
             TEST_FUNC(test_strings_split),
             TEST_FUNC(test_tokenize_query_kind),
             TEST_FUNC(test_parse_query),
             TEST_FUNC(test_tokenize_query_loc),
             TEST_FUNC(test_parse_csv),
             {NULL, NULL}};
#endif

#ifdef DEBUG_MAIN
int main() { return 0; }
#endif