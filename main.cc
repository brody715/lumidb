#include <iostream>
#include <string>

#include "argumentum/argparse.h"
#include "lumidb/db.hh"
#include "lumidb/repl.hh"

using namespace std;
using namespace argumentum;
using namespace lumidb;

struct CliOptions {
  string in_script;
};

int main(int argc, char **argv) {
  CliOptions opts;

  auto parser = argument_parser{};
  auto params = parser.params();

  parser.config().program(argv[0]).description(
      "A db and a simple student manage system.");

  params.add_parameter(opts.in_script, "--in-script")
      .help("The input script file.");

  if (!parser.parse_args(argc, argv)) {
    return 1;
  }

  auto db_res = lumidb::create_database(lumidb::CreateDatabaseParams{});
  if (db_res.has_error()) {
    std::cout << db_res.unwrap_err().to_string() << std::endl;
    return 1;
  }

  auto repl = lumidb::REPL(db_res.unwrap());

  auto res = repl.init();
  if (res.has_error()) {
    std::cout << res.unwrap_err().to_string() << std::endl;
    return 1;
  }

  return repl.run_loop();
}
