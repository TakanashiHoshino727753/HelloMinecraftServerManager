# 统一日志模块（hmcsm_logging）：desktop / remote 两端共用的消息处理器。
# 仅依赖 Qt6::Core（QCoreApplication / QFile / QDateTime 等），无网络/多媒体依赖。
add_library(hmcsm_logging STATIC
    hmcsm_logging.cpp
    hmcsm_logging.h
)

target_link_libraries(hmcsm_logging PUBLIC Qt6::Core)
target_include_directories(hmcsm_logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
