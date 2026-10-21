# Settings Editor（设置管理）：外观属性委托给 shared 的 AppearanceSettings
add_library(hmcsm_settings STATIC
    settingscontroller.h
    settingscontroller.cpp
)
target_link_libraries(hmcsm_settings PUBLIC Qt6::Core hmcsm_appearance)
target_include_directories(hmcsm_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/shared)
