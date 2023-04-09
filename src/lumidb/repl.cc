#include "lumidb/repl.hh"

#include <iostream>
#include <memory>

#include "../../third-party/isocline/src/completions.h"
#include "isocline.h"
#include "lumidb/function.hh"
#include "lumidb/table.hh"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"

using namespace std;
using namespace lumidb;

// Begin Autocomplete

namespace details {
static void nop_completer_func(ic_completion_env_t *cenv, const char *prefix){};

static void word_completer(ic_completion_env_t *cenv, const char *word) {
  static const char *completions[] = {"exit", NULL};
  if (!ic_add_completions(cenv, word, completions)) {
    return;
  }

  auto completer = reinterpret_cast<AutoCompleter *>(ic_completion_arg(cenv));
  if (completer == nullptr) {
    return;
  }

  auto items = completer->complete(word);
  for (auto &item : items) {
    ic_add_completion_ex(cenv, item->completion.c_str(), item->display.c_str(),
                         item->help.c_str());
  }
}

static void completer_func(ic_completion_env_t *cenv, const char *prefix) {
  // try to complete file names from the roots "." and "/usr/local"
  ic_complete_filename(cenv, prefix, 0, ".", nullptr /* any extension */);

  ic_complete_word(cenv, prefix, &word_completer, nullptr);
}
}  // namespace details

void AutoCompleter::init() {
  ic_set_history("lumidb_history.txt", -1);
  ic_set_default_completer(&details::completer_func, this);
  ic_enable_auto_tab(true);

  auto funcs = db_->list_functions();
  if (!funcs.has_error()) {
    prefix_complete_tree_.clear();
    for (auto &func : funcs.unwrap()) {
      auto item = AutoCompleteItem{};
      item.completion = func->name();
      item.display = helper::format_function(*func);
      item.help = func->description();
      prefix_complete_tree_.insert(func->name(), item);
    }
  }
}

std::vector<const AutoCompleteItem *> AutoCompleter::complete(
    std::string_view prefix) {
  auto items = prefix_complete_tree_.find_prefix(prefix);
  return items;
}

AutoCompleter::~AutoCompleter() {
  ic_set_default_completer(&details::nop_completer_func, nullptr);
}

// End Autocomplete

static bool my_readline(const std::string &prompt, std::string &line) {
  char *input;
  input = ic_readline(prompt.c_str());
  if (input == nullptr) {
    return false;
  }

  line = input;
  free(input);
  return true;
}

Result<bool> REPL::init() {
  completer_.init();
  return true;
}

REPL::~REPL() {}

int REPL::run_loop() {
  while (true) {
    std::string line;
    // std::cout << "lumidb> ";
    if (!my_readline("lumidb", line)) {
      break;
    }

    if (line == "exit") {
      break;
    }

    line = trim(line);

    if (line.empty()) {
      continue;
    }

    auto query_res = parse_query(line);

    if (query_res.has_error()) {
      std::cout << query_res.unwrap_err().to_string() << std::endl;
      continue;
    }

    auto result = db_->execute(query_res.unwrap());
    if (result.is_ok()) {
      auto table = result.unwrap();
      table->dump(std::cout) << std::endl;
    } else {
      std::cout << result.unwrap_err().to_string() << std::endl;
    }
  }

  return 0;
};