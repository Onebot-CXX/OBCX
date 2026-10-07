#pragma once

// The build supplies a package-specific alias to the shared implementation.
// Host runtime utilities use reflected_actor_impl.hpp directly.
#ifndef OBCX_ACTOR_BINDING_HEADER
#error                                                                         \
    "OBCX_ACTOR_METADATA_REQUIRED: build the actor with obcx_add_actor and register its implementation targets with obcx_package_target"
#else
#include OBCX_ACTOR_BINDING_HEADER
#endif
