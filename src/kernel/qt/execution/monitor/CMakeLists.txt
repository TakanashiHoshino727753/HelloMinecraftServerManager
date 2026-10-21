# Performance Monitor（性能监视）：整机 CPU / 内存采样
# 头文件内联实现（无 .cpp），随架构调整从 main.cpp 下沉为独立模块。
add_library(hmcsm_monitor STATIC
    systemmonitor.h
)
target_link_libraries(hmcsm_monitor PUBLIC Qt6::Core)
target_include_directories(hmcsm_monitor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
