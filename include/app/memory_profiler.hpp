#pragma once

#include <string>

namespace obcx::app {

// Application-only diagnostics. No jemalloc dependency is exported by the SDK.
[[nodiscard]] auto memory_profiling_enabled() noexcept -> bool;
[[nodiscard]] auto memory_profile_stats() -> std::string;
// The caller supplies a filename prefix. Missing parent directories are
// created. Relative paths resolve against the process working directory; dumps
// return full paths. Each dump gets a unique owner-only filename; existing
// dumps are not replaced.
[[nodiscard]] auto memory_profile_dump(const std::string &prefix)
    -> std::string;

} // namespace obcx::app
