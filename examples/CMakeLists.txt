add_executable(qq_bot QQBot.cpp)
target_link_libraries(qq_bot PRIVATE obcx_core)

add_executable(telegram_bot TelegramBot.cpp)
target_link_libraries(telegram_bot PRIVATE obcx_core)

add_subdirectory(plugins)

