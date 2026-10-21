# 磁盘文件访问控制：路径规范化 / 根目录白名单 / 原子写入
add_library(hmcsm_diskaccess STATIC
    diskaccess.h
    diskaccess.cpp
)
target_link_libraries(hmcsm_diskaccess PUBLIC Qt6::Core)
target_include_directories(hmcsm_diskaccess PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
