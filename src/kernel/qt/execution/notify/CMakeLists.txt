# Webhook 通知（崩溃 / 启停 / 玩家进服推送）
add_library(hmcsm_notify STATIC
    notifier.h
    notifier.cpp
)
target_link_libraries(hmcsm_notify PUBLIC Qt6::Core Qt6::Network)
target_include_directories(hmcsm_notify PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
