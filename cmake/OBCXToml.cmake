# Use the same parser configuration in the runtime and installed SDK consumers.
# A distribution's precompiled tomlplusplus library cannot enable experimental
# syntax through consumer definitions alone, so use only its headers here.
if(NOT TARGET obcx::tomlplusplus)
  add_library(obcx::tomlplusplus INTERFACE IMPORTED)
  set_target_properties(obcx::tomlplusplus PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:tomlplusplus::tomlplusplus,INTERFACE_INCLUDE_DIRECTORIES>"
    INTERFACE_COMPILE_DEFINITIONS "TOML_HEADER_ONLY=1;TOML_SHARED_LIB=0;TOML_ENABLE_UNRELEASED_FEATURES=1")
endif()
