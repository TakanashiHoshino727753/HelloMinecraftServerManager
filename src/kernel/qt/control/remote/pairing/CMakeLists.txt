# 远控端配对与控制反代（独立于 WebUI 的长期设备凭证配对，端口 25577）
# qrimageprovider 供 QML 以 image://qr/<text> 渲染配对二维码
add_library(hmcsm_remotecontrol STATIC
    remotecontrolserver.h
    remotecontrolserver.cpp
    qrimageprovider.h
    qrimageprovider.cpp
)
# Qt6::Qml / Qt6::Quick 供 QrImageProvider（QQuickImageProvider + QJSEngine）使用
target_link_libraries(hmcsm_remotecontrol PUBLIC Qt6::Core Qt6::Network Qt6::Gui Qt6::Qml Qt6::Quick
    hmcsm_settings hmcsm_qrcode)
target_include_directories(hmcsm_remotecontrol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
