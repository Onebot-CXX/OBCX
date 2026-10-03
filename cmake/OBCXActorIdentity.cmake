include_guard(GLOBAL)

# Internal helper shared by package registration and explicit host test fixtures.
# Production callers obtain these required values from admitted package metadata.
function(_obcx_bind_actor_identity TARGET_NAME PACKAGE_ID ACTOR_NAME ACTOR_VERSION OUTPUT_DIR)
  if("${PACKAGE_ID}" STREQUAL "" OR "${ACTOR_NAME}" STREQUAL "" OR "${ACTOR_VERSION}" STREQUAL "")
    message(FATAL_ERROR "Actor identity requires explicit package id, actor name and version")
  endif()
  string(SHA256 _identity "${PACKAGE_ID}|${ACTOR_NAME}|${ACTOR_VERSION}")
  set(_namespace "obcx::generated::actor_${_identity}")
  foreach(_field ACTOR_NAME ACTOR_VERSION)
    string(REPLACE "\\" "\\\\" ${_field} "${${_field}}")
    string(REPLACE "\"" "\\\"" ${_field} "${${_field}}")
    string(REPLACE "\n" "\\n" ${_field} "${${_field}}")
    string(REPLACE "\r" "\\r" ${_field} "${${_field}}")
  endforeach()
  file(MAKE_DIRECTORY "${OUTPUT_DIR}")
  set(_metadata "${OUTPUT_DIR}/actor_identity.hpp")
  set(_binding "${OUTPUT_DIR}/actor_binding.hpp")
  # file(CONFIGURE) only changes timestamps when content changes.
  set(_metadata_content "#pragma once\n#include <string_view>\nnamespace ${_namespace} {\nstruct Identity {\n  static constexpr std::string_view actor_name = \"${ACTOR_NAME}\";\n  static constexpr std::string_view actor_version = \"${ACTOR_VERSION}\";\n};\n}\n#define OBCX_ACTOR_NAME \"${ACTOR_NAME}\"\n#define OBCX_ACTOR_VERSION \"${ACTOR_VERSION}\"\n")
  set(_binding_content "#pragma once\n#include \"actor_identity.hpp\"\n#include \"core/actor/reflected_actor_impl.hpp\"\nnamespace ${_namespace} {\ntemplate <typename Derived>\nusing ReflectedActor = ::obcx::core::detail::ReflectedActorImpl<Derived, Identity>;\n}\nnamespace obcx::core {\nusing ::${_namespace}::ReflectedActor;\n}\n")
  file(CONFIGURE OUTPUT "${_metadata}" CONTENT "${_metadata_content}" @ONLY)
  file(CONFIGURE OUTPUT "${_binding}" CONTENT "${_binding_content}" @ONLY)
  target_compile_definitions("${TARGET_NAME}" PRIVATE
    "OBCX_ACTOR_METADATA_HEADER=\"${_metadata}\""
    "OBCX_ACTOR_BINDING_HEADER=\"${_binding}\"")
endfunction()
