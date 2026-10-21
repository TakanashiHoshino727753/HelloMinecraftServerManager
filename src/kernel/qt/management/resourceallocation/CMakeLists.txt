# 系统资源分配：下载并发配额 / 端口预约 / 内存与 CPU 策略
add_library(hmcsm_resource STATIC
    resourceallocator.h
    resourceallocator.cpp
)
target_link_libraries(hmcsm_resource PUBLIC Qt6::Core Qt6::Network)
target_include_directories(hmcsm_resource PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
