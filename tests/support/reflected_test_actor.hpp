#pragma once

#include "core/actor/reflected_actor_impl.hpp"

namespace obcx::test {

// Host-only test doubles deliberately carry explicit, independent identities.
// A single test translation unit may exercise several actors without a package.
// This adapter is not installed and is not a production metadata fallback.
template <typename Derived>
using ReflectedActor = core::detail::ReflectedActorImpl<Derived, Derived>;

} // namespace obcx::test

namespace obcx::core {
using ::obcx::test::ReflectedActor;
}
