# Interface / CLI-Remote（远程命令行接口）：连远端 HMCSM 实例的 WebUI REST API
qt_add_executable(HMCSM-CLI-Remote main.cpp)

target_link_libraries(HMCSM-CLI-Remote PRIVATE
    Qt6::Core
    Qt6::Network
    hmcsm_remoteclient
    hmcsm_logging
)

install(TARGETS HMCSM-CLI-Remote RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
