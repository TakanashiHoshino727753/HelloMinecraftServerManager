# NapCat / NoneBot 机器人控制（独立管理两个 bot 外部进程 + 内置 25585 控制通道）
add_library(hmcsm_bot STATIC
    botcontroller.h
    botcontroller.cpp
)
target_link_libraries(hmcsm_bot PUBLIC Qt6::Core Qt6::Network Qt6::HttpServer
    hmcsm_download hmcsm_server hmcsm_settings hmcsm_server)
target_include_directories(hmcsm_bot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
