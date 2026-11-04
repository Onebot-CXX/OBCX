find_package(tomlplusplus CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS program_options)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
  add_executable(obcx main.cpp memory_profiler.cpp)
  if(OBCX_ENABLE_JEMALLOC_PROFILING)
    # Put jemalloc in the executable's lookup scope ahead of libc so core
    # and actors use it too; diagnostics also verify interposition at runtime.
    target_link_libraries(obcx PRIVATE PkgConfig::OBCX_JEMALLOC ${CMAKE_DL_LIBS})
    target_compile_definitions(obcx PRIVATE OBCX_ENABLE_JEMALLOC_PROFILING=1)
    target_compile_options(obcx PRIVATE -fno-omit-frame-pointer)
    set_target_properties(obcx PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
  endif()
  target_link_libraries(obcx PRIVATE obcx_core tomlplusplus::tomlplusplus
                                     Boost::program_options)

  # Keep the installed application relocatable: the runtime library is
  # installed beside it under the same prefix, not into a system search path.
  if(APPLE)
    set_target_properties(obcx PROPERTIES
      INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
  elseif(UNIX)
    set_target_properties(obcx PROPERTIES
      INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  endif()

  install(TARGETS obcx RUNTIME DESTINATION bin)
endif()
