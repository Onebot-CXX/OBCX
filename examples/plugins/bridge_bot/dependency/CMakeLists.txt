add_library(
  bridge_core SHARED
  config.cpp
  media_processor.cpp
  retry_queue_manager.cpp
  path_manager.cpp
  # qq
  qq/qq_command_handler.cpp
  qq/qq_event_handler.cpp
  qq/qq_handler.cpp
  qq/qq_media_processor.cpp
  qq/qq_message_formatter.cpp
  # telegram
  telegram/telegram_handler
  telegram/telegram_command_handler.cpp
  telegram/telegram_event_handler.cpp
  telegram/telegram_media_processor.cpp
  telegram/telegram_message_formatter.cpp
  telegram/telegram_handler.cpp
  # database
  database/database_manager.cpp)

target_link_libraries(bridge_core PUBLIC obcx_core unofficial::sqlite3::sqlite3)
