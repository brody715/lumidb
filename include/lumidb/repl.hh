#pragma once

#include <cstdint>
#include <deque>
#include <istream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "lumidb/db.hh"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"

namespace lumidb {

// Store a trie tree of values, and allow to find all values with a given prefix
template <typename Val>
class TrieTree {
 public:
  TrieTree() : root_(std::make_unique<Node>()) {}

  void clear() { root_ = std::make_unique<Node>(); }

  void insert(std::string_view key, Val val) {
    auto idx = values_.size();
    values_.push_back(val);

    Val *value_ptr = &values_[idx];

    Node *cur = root_.get();
    cur->subtree_vals.push_back(value_ptr);

    for (auto c : key) {
      auto it = cur->children.find(c);
      if (it == cur->children.cend()) {
        cur->children[c] = std::make_unique<Node>();
      }
      cur = cur->children[c].get();
      cur->subtree_vals.push_back(value_ptr);
    }
  }

  void find_prefix(std::string_view prefix,
                   std::vector<const Val *> &result) const {
    Node *cur = root_.get();
    for (auto c : prefix) {
      auto it = cur->children.find(c);
      if (it == cur->children.cend()) {
        return;
      }
      cur = it->second.get();
    }

    for (auto val : cur->subtree_vals) {
      result.push_back(val);
    }
  }

 private:
  struct Node {
    std::unordered_map<char, std::unique_ptr<Node>> children;
    std::vector<Val *> subtree_vals;
  };

 private:
  std::unique_ptr<Node> root_;
  std::deque<Val> values_;
};

struct AutoCompleteItem {
  std::string completion;
  std::string display;
  std::string help;
};

struct HighlightItem {
  size_t pos;
  size_t cnt;
  const char *style;
};

class AutoCompleter {
 public:
  enum CompleteType : int { Function = 01, Table = 10 };

  AutoCompleter(DatabasePtr db_) : db_(db_) {}
  ~AutoCompleter();

  void init();
  std::vector<const AutoCompleteItem *> complete(CompleteType type,
                                                 std::string_view prefix);

  std::vector<HighlightItem> highlight(std::string_view input);

  // check reload: check whether we should reload the complete items to sync
  // with current db states
  void check_reload();

 private:
  void reload_complete_items();

 private:
  int64_t prev_version_ = 0;
  DatabasePtr db_;
  TrieTree<AutoCompleteItem> functions_{};
  TrieTree<AutoCompleteItem> table_and_fields_{};
};

class REPL {
 public:
  REPL(DatabasePtr db);
  ~REPL();

  Result<bool> init();

  // pre run a script
  void pre_run(std::istream &in);

  // start REPL
  int run_loop();

 private:
  bool handle_input(std::string_view input);

 private:
  DatabasePtr db_;
  AutoCompleter completer_;
  LoggerPtr logger_;
};
}  // namespace lumidb