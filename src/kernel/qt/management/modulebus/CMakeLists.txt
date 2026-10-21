# 模块间调用（Inter-Module Invocation）：服务注册表 + 事件总线
add_library(hmcsm_modulebus STATIC
    modulebus.h
    modulebus.cpp
)
target_link_libraries(hmcsm_modulebus PUBLIC Qt6::Core)
target_include_directories(hmcsm_modulebus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
