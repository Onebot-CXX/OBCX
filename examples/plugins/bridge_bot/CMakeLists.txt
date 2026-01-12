find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(tomlplusplus REQUIRED)

include_directories(include)

add_subdirectory(dependency)
add_subdirectory(qq_to_tg)
add_subdirectory(tg_to_qq)
