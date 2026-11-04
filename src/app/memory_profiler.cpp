#include "app/memory_profiler.hpp"

#ifdef OBCX_ENABLE_JEMALLOC_PROFILING
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fmt/format.h>
#include <jemalloc/jemalloc.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#endif

namespace obcx::app {
namespace {

[[maybe_unused]] constexpr auto kUnavailable =
    "memory_profile_unavailable: rebuild with "
    "-DOBCX_ENABLE_JEMALLOC_PROFILING=ON";

#ifdef OBCX_ENABLE_JEMALLOC_PROFILING
void control(const char *name, void *output, std::size_t *output_size,
             void *input, std::size_t input_size) {
  const auto error = mallctl(name, output, output_size, input, input_size);
  if (error != 0) {
    throw std::runtime_error(
        fmt::format("{}: {}", name,
                    std::error_code(error, std::generic_category()).message()));
  }
}

template <typename T> auto read_control(const char *name) -> T {
  T result{};
  auto size = sizeof(result);
  control(name, &result, &size, nullptr, 0);
  return result;
}

void verify_allocator() {
  Dl_info allocation{};
  Dl_info controls{};
  if (::dladdr(::dlsym(RTLD_DEFAULT, "malloc"), &allocation) == 0 ||
      ::dladdr(::dlsym(RTLD_DEFAULT, "mallctl"), &controls) == 0 ||
      allocation.dli_fbase != controls.dli_fbase) {
    throw std::runtime_error(
        "malloc is not provided by the linked jemalloc; remove conflicting "
        "allocator preloads before using these statistics");
  }
}

void refresh_stats() {
  std::uint64_t epoch = 1;
  auto size = sizeof(epoch);
  control("epoch", &epoch, &size, &epoch, sizeof(epoch));
}

constexpr auto kEnableSampling =
    "set MALLOC_CONF=prof:true,prof_active:true,lg_prof_sample:19 before "
    "starting the process to enable sampled allocation stacks";
#endif

} // namespace

auto memory_profiling_enabled() noexcept -> bool {
#ifdef OBCX_ENABLE_JEMALLOC_PROFILING
  return true;
#else
  return false;
#endif
}

auto memory_profile_stats() -> std::string {
#ifdef OBCX_ENABLE_JEMALLOC_PROFILING
  try {
    verify_allocator();
    const auto profiling_supported = read_control<bool>("config.prof");
    const auto sampling_enabled = read_control<bool>("opt.prof");
    const auto sampling_active =
        sampling_enabled && read_control<bool>("prof.active");
    refresh_stats();
    const auto allocated = read_control<std::size_t>("stats.allocated");
    const auto active = read_control<std::size_t>("stats.active");
    const auto resident = read_control<std::size_t>("stats.resident");
    const auto mapped = read_control<std::size_t>("stats.mapped");
    const auto retained = read_control<std::size_t>("stats.retained");
    const auto metadata = read_control<std::size_t>("stats.metadata");
    return fmt::format(
        "jemalloc version={} profiling_supported={} sampling_enabled={} "
        "sampling_active={}\n"
        "heap bytes: allocated={} active={} resident={} mapped={} retained={} "
        "metadata={}{}",
        read_control<const char *>("version"), profiling_supported,
        sampling_enabled, sampling_active, allocated, active, resident, mapped,
        retained, metadata,
        sampling_active ? std::string{} : fmt::format("\n{}", kEnableSampling));
  } catch (const std::exception &error) {
    return fmt::format("memory_profile_error: {}", error.what());
  }
#else
  return kUnavailable;
#endif
}

auto memory_profile_dump([[maybe_unused]] const std::string &prefix)
    -> std::string {
#ifdef OBCX_ENABLE_JEMALLOC_PROFILING
  try {
    verify_allocator();
    if (!read_control<bool>("config.prof")) {
      return "memory_profile_unavailable: jemalloc lacks profiling support";
    }
    if (!read_control<bool>("opt.prof")) {
      return fmt::format("memory_profile_inactive: {}", kEnableSampling);
    }
    const auto sampling_active = read_control<bool>("prof.active");
    const std::filesystem::path path{prefix};
    if (prefix.empty() || path.filename().empty()) {
      return "memory_profile_error: memory_dump requires a filename prefix, "
             "e.g. memory_dump=./obcx";
    }
    // Resolve relative paths once so the reported location matches the dump.
    const auto absolute_path = std::filesystem::absolute(path);
    std::filesystem::create_directories(absolute_path.parent_path());
    // Create first, with mode 0600, rather than letting prof.dump create a
    // potentially world-readable file or overwrite an earlier snapshot.
    auto filename = absolute_path.string() + ".XXXXXX.heap";
    const auto descriptor = ::mkstemps(filename.data(), 5);
    if (descriptor < 0) {
      return fmt::format(
          "memory_profile_error: cannot create snapshot: {}",
          std::error_code(errno, std::generic_category()).message());
    }
    if (::close(descriptor) != 0) {
      const auto error = errno;
      ::unlink(filename.c_str());
      return fmt::format(
          "memory_profile_error: cannot close snapshot: {}",
          std::error_code(error, std::generic_category()).message());
    }
    const char *destination = filename.c_str();
    try {
      control("prof.dump", nullptr, nullptr, &destination, sizeof(destination));
    } catch (...) {
      ::unlink(filename.c_str());
      throw;
    }
    return fmt::format(
        "memory_profile_dump: {} (sampling_active={}; sampled live heap for "
        "the whole process; preserve binaries and staged actor DSOs before "
        "reload/shutdown for symbolization)",
        filename, sampling_active);
  } catch (const std::exception &error) {
    return fmt::format("memory_profile_error: {}", error.what());
  }
#else
  return kUnavailable;
#endif
}

} // namespace obcx::app
