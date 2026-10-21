# Interface / CLI（命令行接口）：与 GUI 共用同一个内核，只是把结果打印到 stdout
qt_add_executable(HMCSM-CLI-Local main.cpp)

target_link_libraries(HMCSM-CLI-Local PRIVATE
    Qt6::Core
    Qt6::Network
    hmcsm_kernel
    hmcsm_logging
)

install(TARGETS HMCSM-CLI-Local RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
