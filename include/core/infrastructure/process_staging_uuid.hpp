#ifndef OBCX_INCLUDE_CORE_PROCESS_STAGING_UUID_HPP_
#define OBCX_INCLUDE_CORE_PROCESS_STAGING_UUID_HPP_

#include <string_view>

namespace obcx::core::detail {

// Created when obcx_core is initialized and reused by every staging path in
// the process.
[[nodiscard]] auto process_staging_uuid() noexcept -> std::string_view;

} // namespace obcx::core::detail

#endif // OBCX_INCLUDE_CORE_PROCESS_STAGING_UUID_HPP_
