#include "lumidb/repl.hh"

#include <cstdlib>
#include <iostream>
#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../../third-party/isocline/src/completions.h"
#include "isocline.h"
#include "lumidb/function.hh"
#include "lumidb/query.hh"
#include "lumidb/table.hh"
#include "lumidb/types.hh"
#include "lumidb/utils.hh"

using namespace std;
using namespace lumidb;

class ConsoleLogger : public Logger {
 public:
  void log(Logger::LogLevel level, const std::string &msg) override {
    switch (level) {
      case LogLevel::ERROR:
        ic_printf("[red]\\[error]: %s[/red]\n", msg.c_str());
        break;
      case LogLevel::WARNING:
        ic_printf("[yellow]\\[warn]: %s[/yellow]\n", msg.c_str());
        break;
      case LogLevel::INFO:
        ic_printf("[green]\\[info]: %s[/green]\n", msg.c_str());
        break;
      case LogLevel::DEBUG:
        ic_printf("[blue]\\[debug]: %s[/blue]\n", msg.c_str());
        break;
    }
  }
};

// Begin Autocomplete

namespace details {

static void word_completer(ic_completion_env_t *cenv, const char *word) {
  static const char *completions[] = {"exit", NULL};
  if (!ic_add_completions(cenv, word, completions)) {
    return;
  }

  auto completer = reinterpret_cast<AutoCompleter *>(ic_completion_arg(cenv));
  if (completer == nullptr) {
    return;
  }

  completer->check_reload();

  if (word[0] == '"' || word[0] == '\'') {
    // try to complete table names
    auto items = completer->complete(AutoCompleter::Table, (word + 1));

    for (auto &item : items) {
      // it's safe to allocate temporary string here, because ic will copy const
      // char*
      std::string completion = word[0] + item->completion + word[0];
      std::string display = word[0] + item->display + word[0];
      ic_add_completion_ex(cenv, completion.c_str(), display.c_str(),
                           item->help.c_str());
    }
    return;
  }

  // try to complete from the list of functions
  auto items = completer->complete(AutoCompleter::Function, word);
  for (auto &item : items) {
    ic_add_completion_ex(cenv, item->completion.c_str(), item->display.c_str(),
                         item->help.c_str());
  }
}

static void completer_func_nop(ic_completion_env_t *cenv, const char *prefix){};

static void completer_func(ic_completion_env_t *cenv, const char *prefix) {
  // try to complete file names from the roots "." and "/usr/local"
  // ic_complete_filename(cenv, prefix, 0, ".", ".so" /* any extension */);

  ic_complete_word(cenv, prefix, &word_completer, nullptr);
}

static void highlighter_func_nop(ic_highlight_env_t *henv, const char *word,
                                 void *arg) {}
static void highlighter_func(ic_highlight_env_t *henv, const char *word,
                             void *arg) {
  auto tokens = tokenize_query(word);

  auto completer = reinterpret_cast<AutoCompleter *>(arg);
  if (completer == nullptr) {
    return;
  }

  auto items = completer->highlight(word);
  for (auto &item : items) {
    ic_highlight(henv, item.pos, item.cnt, item.style);
  }
}
}  // namespace details

void AutoCompleter::init() {
  ic_set_history("lumidb_history.txt", -1);
  ic_set_default_completer(&details::completer_func, this);
  ic_set_default_highlighter(&details::highlighter_func, this);
  ic_enable_auto_tab(true);

  reload_complete_items();
}

AutoCompleter::~AutoCompleter() {
  ic_set_default_completer(&details::completer_func_nop, nullptr);
  ic_set_default_highlighter(&details::highlighter_func_nop, nullptr);
}

void AutoCompleter::check_reload() {
  auto version = db_->version();
  if (version != prev_version_) {
    reload_complete_items();
    prev_version_ = version;
  }
}

void AutoCompleter::reload_complete_items() {
  auto funcs = db_->list_functions();
  if (!funcs.has_error()) {
    functions_.clear();
    for (auto &func : funcs.unwrap()) {
      auto item = AutoCompleteItem{};
      item.completion = func->name();
      item.display = helper::format_function(*func);
      item.help = func->description();
      functions_.insert(func->name(), item);
    }
  }

  auto tables = db_->list_tables();
  if (!tables.has_error()) {
    table_and_fields_.clear();
    for (auto &table : tables.unwrap()) {
      auto item = AutoCompleteItem{};
      item.completion = table->name();
      item.display = table->name();
      item.help = "";
      table_and_fields_.insert(table->name(), item);

      auto fields = table->schema().field_names();
      for (auto &field : fields) {
        auto item = AutoCompleteItem{};
        item.completion = field;
        item.display = field;
        item.help = "";
        table_and_fields_.insert(field, item);
      }
    }
  }
}

std::vector<const AutoCompleteItem *> AutoCompleter::complete(
    AutoCompleter::CompleteType type, std::string_view prefix) {
  vector<const AutoCompleteItem *> items;
  if (type & AutoCompleter::Table) {
    table_and_fields_.find_prefix(prefix, items);
  }

  if (type & AutoCompleter::Function) {
    functions_.find_prefix(prefix, items);
  }
  return items;
}

std::vector<HighlightItem> AutoCompleter::highlight(std::string_view query) {
  using qtk = QueryTokenKind;

  std::vector<HighlightItem> items;
  auto tokens = tokenize_query(query);
  for (auto &token : tokens) {
    const char *color = nullptr;
    switch (token.kind) {
      case QueryTokenKind::Identifier:
        color = "type";
        break;
      case QueryTokenKind::StringLiteral:
        color = "string";
        break;
      case QueryTokenKind::FloatLiteral:
        color = "number";
        break;
      case QueryTokenKind::Pipe:
        color = "keyword";
        break;
      case QueryTokenKind::L_Paren:
      case QueryTokenKind::R_Paren:
      case QueryTokenKind::Comma:
      case QueryTokenKind::EOS:
      case QueryTokenKind::ErrorToken:
        color = nullptr;
        break;
    }

    items.push_back({token.loc.column_start,
                     token.loc.column_end - token.loc.column_start, color});
  }

  return items;
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

REPL::REPL(DatabasePtr db)
    : db_(db), completer_(db_), logger_(make_shared<ConsoleLogger>()) {}

Result<bool> REPL::init() {
  completer_.init();
  db_->set_logger(logger_);

  return true;
}

REPL::~REPL() {}

void REPL::pre_run(std::istream &in) {
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty()) {
      continue;
    }

    logger_->log(logger_->INFO, fmt::format("executing: {}", line));

    if (!handle_input(line)) {
      break;
    }
  }
}

int REPL::run_loop() {
  while (true) {
    std::string line;
    // std::cout << "lumidb> ";
    if (!my_readline("lumidb", line)) {
      break;
    }

    if (!handle_input(line)) {
      break;
    }
  }

  return 0;
};

// return false if exit
bool REPL::handle_input(std::string_view input) {
  if (input == "exit") {
    return false;
  }

  input = trim(input);

  if (input.empty()) {
    return true;
  }

  if (input[0] == '!') {
    auto cmd = input.substr(1);
    std::system(cmd.data());
    return true;
  }

  auto query_res = parse_query(input);

  if (query_res.has_error()) {
    logger_->log(logger_->ERROR, query_res.unwrap_err().to_string());
    return true;
  }

  auto result = db_->execute(query_res.unwrap());
  if (result.is_ok()) {
    auto table = result.unwrap();
    table->dump(std::cout) << std::endl;
  } else {
    logger_->log(logger_->ERROR, result.unwrap_err().to_string());
  }

  return true;
}
