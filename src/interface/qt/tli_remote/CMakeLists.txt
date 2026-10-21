# Interface / TLI-Remote（远程文本行接口）：连远端 HMCSM 实例的 WebUI REST API
qt_add_executable(HMCSM-TLI-Remote main.cpp)

target_link_libraries(HMCSM-TLI-Remote PRIVATE
    Qt6::Core
    Qt6::Network
    hmcsm_remoteclient
    hmcsm_logging
)

install(TARGETS HMCSM-TLI-Remote RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
