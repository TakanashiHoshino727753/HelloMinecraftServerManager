# Interface / TLI（文本行接口）：交互式文本会话，与 GUI 共用同一个内核
qt_add_executable(HMCSM-TLI-Local main.cpp)

target_link_libraries(HMCSM-TLI-Local PRIVATE
    Qt6::Core
    Qt6::Network
    hmcsm_kernel
    hmcsm_logging
)

install(TARGETS HMCSM-TLI-Local RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
