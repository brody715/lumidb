#include <iostream>

#include "argumentum/argparse.h"

using namespace std;
using namespace argumentum;

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

  return 0;
}