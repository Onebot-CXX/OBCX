#include "core/infrastructure/process_staging_uuid.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

namespace obcx::core::detail {
namespace {

const std::string process_start_staging_uuid =
    boost::uuids::to_string(boost::uuids::random_generator{}());

} // namespace

auto process_staging_uuid() noexcept -> std::string_view {
  return process_start_staging_uuid;
}

} // namespace obcx::core::detail
