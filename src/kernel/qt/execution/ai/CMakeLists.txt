# AI Integration（AI 集成）—— 占位模块，功能待补
add_library(hmcsm_ai STATIC
    ai_integration.h
    ai_integration.cpp
)
target_link_libraries(hmcsm_ai PUBLIC Qt6::Core)
target_include_directories(hmcsm_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
