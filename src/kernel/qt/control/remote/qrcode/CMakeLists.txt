# 内嵌的第三方二维码生成库（含 JS 与 C++ 实现），头文件形式，被 webui / pairing 双方直接 #include。
# 因为两端都要用，故提升为 control/remote 下的公共模块，避免 webui 与 pairing 互相依赖。
add_library(hmcsm_qrcode INTERFACE)
target_include_directories(hmcsm_qrcode INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(hmcsm_qrcode INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/qrcode_lib.h)
