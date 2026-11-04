#include "core/actor/actor_package_stager.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

// Offline measurement only: stage files, never discover or activate actors.
// Interactive mode lets an operator change disposable files between stages.
namespace {

class Probe {
public:
  explicit Probe(std::filesystem::path root) : root_(std::move(root)) {}

  auto stage(const std::filesystem::path &library) -> bool {
    const auto started = std::chrono::steady_clock::now();
    auto result = stager_.stage(
        {
            .actor_library = library,
            .staging_root = root_,
            .actor_name = library.stem().string(),
            .generation_id = generation_,
            .expected_process_owned_dependencies = expected_,
        },
        *context_);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started);
    std::cout << "stage " << std::quoted(library.string()) << " ms="
              << elapsed.count() << " ok=" << static_cast<bool>(result)
              << '\n';
    if (!result) {
      std::cout << "failure " << result.code << ' '
                << std::quoted(result.message) << std::endl;
      return false;
    }
    for (const auto &[name, identity] :
         result.package->process_owned_dependencies()) {
      std::cout << "process " << std::quoted(name) << ' '
                << std::quoted(identity.path) << ' ' << identity.digest << '\n';
    }
    for (const auto &dependency : result.package->private_dependencies()) {
      std::cout << "private " << std::quoted(dependency.source_path.string())
                << ' ' << std::quoted(dependency.staged_identity) << '\n';
    }
    // Destruction removes only this invocation's unique staged package.
    result.package.reset();
    std::cout << "done" << std::endl;
    return true;
  }

  void reset() {
    context_ = std::make_unique<obcx::core::ActorPackageStagingContext>();
    ++generation_;
  }

  void expect(std::string identity, std::string digest) {
    expected_[std::move(identity)] = {.path = {}, .digest = std::move(digest)};
  }

  void clear_expected() { expected_.clear(); }

private:
  std::filesystem::path root_;
  obcx::core::ActorPackageStager stager_;
  std::unique_ptr<obcx::core::ActorPackageStagingContext> context_ =
      std::make_unique<obcx::core::ActorPackageStagingContext>();
  std::uint64_t generation_ = 1;
  std::map<std::string, obcx::core::ProcessOwnedDependencyIdentity> expected_;
};

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 3) {
      throw std::runtime_error(
          "usage: actor_package_staging_probe ROOT batch ITERATIONS LIB... | "
          "ROOT interactive\n"
          "interactive commands: stage \"PATH\", expect \"IDENTITY\" DIGEST, "
          "clear-expected, reset, quit");
    }
    Probe probe(argv[1]);
    const std::string mode = argv[2];
    if (mode == "batch" && argc >= 5) {
      const auto iterations = std::stoi(argv[3]);
      if (iterations <= 0) {
        throw std::runtime_error("ITERATIONS must be positive");
      }
      for (int iteration = 0; iteration < iterations; ++iteration) {
        probe.reset();
        const auto started = std::chrono::steady_clock::now();
        for (int index = 4; index < argc; ++index) {
          if (!probe.stage(argv[index])) {
            return 1;
          }
        }
        std::cout << "iteration " << iteration << " total_ms="
                  << std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - started)
                         .count()
                  << std::endl;
      }
      return 0;
    }
    if (mode != "interactive" || argc != 3) {
      throw std::runtime_error("invalid arguments");
    }
    for (std::string line; std::getline(std::cin, line);) {
      std::istringstream input(line);
      std::string command;
      input >> command;
      if (command == "quit") {
        break;
      }
      if (command == "stage") {
        std::string path;
        input >> std::quoted(path);
        probe.stage(path);
      } else if (command == "reset") {
        probe.reset();
      } else if (command == "expect") {
        std::string identity;
        std::string digest;
        input >> std::quoted(identity) >> digest;
        probe.expect(std::move(identity), std::move(digest));
      } else if (command == "clear-expected") {
        probe.clear_expected();
      } else {
        throw std::runtime_error("unknown interactive command");
      }
      std::cout << "ready" << std::endl;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
