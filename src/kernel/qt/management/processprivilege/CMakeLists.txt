# 进程权限管理：提权事实查询 + 子进程权限策略
add_library(hmcsm_privilege STATIC
    processprivilege.h
    processprivilege.cpp
)
target_link_libraries(hmcsm_privilege PUBLIC Qt6::Core)
target_include_directories(hmcsm_privilege PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
