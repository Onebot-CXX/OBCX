#pragma once

#include <string>
#include <vector>

namespace obcx::plugin_cli {

/// Entry point for `obcx plugin <subcommand>`
/// Returns exit code (0 = success)
auto run_plugin_command(int argc, char *argv[]) -> int;

} // namespace obcx::plugin_cli
