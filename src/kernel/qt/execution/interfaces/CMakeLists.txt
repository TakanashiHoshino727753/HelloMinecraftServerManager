# Interfaces Support for other Managers（其它管理器的接口支撑）—— 占位模块，功能待补
add_library(hmcsm_interfaces STATIC
    interfaces_support.h
    interfaces_support.cpp
)
target_link_libraries(hmcsm_interfaces PUBLIC Qt6::Core)
target_include_directories(hmcsm_interfaces PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
