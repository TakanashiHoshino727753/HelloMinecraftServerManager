# Velocity 反向代理聚合（多实例，依赖 server + java 选取后端 JDK）
add_library(hmcsm_proxy STATIC
    proxycontroller.h
    proxycontroller.cpp
    proxymanager.h
    proxymanager.cpp
)
target_link_libraries(hmcsm_proxy PUBLIC Qt6::Core Qt6::Network hmcsm_download hmcsm_server hmcsm_java)
target_include_directories(hmcsm_proxy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
