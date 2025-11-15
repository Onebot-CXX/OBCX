add_executable(qq_bot qq_bot.cpp)
target_link_libraries(qq_bot PRIVATE obcx_core)

add_executable(telegram_bot telegram_bot.cpp)
target_link_libraries(telegram_bot PRIVATE obcx_core)

add_subdirectory(plugins)

