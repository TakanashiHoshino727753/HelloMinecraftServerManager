# Database Support（数据持久化）—— 占位模块，功能待补
add_library(hmcsm_database STATIC
    database_support.h
    database_support.cpp
)
target_link_libraries(hmcsm_database PUBLIC Qt6::Core)
target_include_directories(hmcsm_database PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
