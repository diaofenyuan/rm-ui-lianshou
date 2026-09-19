cmake_minimum_required(VERSION 3.24)
project(rm_ui VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
include(FetchContent)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_WITH_SSL OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_CPACK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(protobuf URL https://codeload.github.com/protocolbuffers/protobuf/zip/refs/tags/v21.12)
FetchContent_Declare(paho URL https://codeload.github.com/eclipse-paho/paho.mqtt.c/zip/refs/tags/v1.3.14)
FetchContent_MakeAvailable(protobuf paho)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Test)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT
    generated/game_status.pb.cc generated/game_status.pb.h
    generated/rm_messages.pb.cc generated/rm_messages.pb.h
  COMMAND protobuf::protoc --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/generated
    --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/game_status.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/rm_messages.proto
  DEPENDS protobuf::protoc proto/game_status.proto proto/rm_messages.proto VERBATIM)
add_library(rm_core src/status.cpp src/assembler.cpp src/receiver.cpp src/video.cpp src/match_state.cpp
  src/map_transform.cpp
  src/status.h src/assembler.h src/receiver.h src/video.h src/match_state.h src/map_transform.h
  generated/game_status.pb.cc generated/rm_messages.pb.cc)
target_include_directories(rm_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(rm_core PUBLIC PAHO_MQTT_STATIC=1)
target_link_libraries(rm_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network protobuf::libprotobuf paho-mqtt3a-static)
add_executable(rm_client src/main.cpp src/window.cpp src/window.h resources.qrc
  src/ui/operator_page.cpp src/ui/operator_page.h
  src/ui/operator_hud.cpp src/ui/operator_hud.h
  src/ui/video_stage.cpp src/ui/video_stage.h
  src/ui/status_strip.cpp src/ui/status_strip.h
  src/ui/teammate_panel.cpp src/ui/teammate_panel.h
  src/ui/console_page.cpp src/ui/console_page.h
  src/ui/score_bar.cpp src/ui/score_bar.h
  src/ui/minimap.cpp src/ui/minimap.h
  src/ui/event_timeline.cpp src/ui/event_timeline.h
  src/ui/respawn_panel.cpp src/ui/respawn_panel.h
  src/ui/analysis_panel.cpp src/ui/analysis_panel.h
  src/ui/video_preview.cpp src/ui/video_preview.h
  src/ui/robot_list.cpp src/ui/robot_list.h src/ui/console_panel.h)
target_link_libraries(rm_client PRIVATE rm_core Qt6::Widgets)
add_executable(rm_receive src/receive_main.cpp)
target_link_libraries(rm_receive PRIVATE rm_core)
enable_testing()
add_executable(rm_tests tests/core_test.cpp)
target_link_libraries(rm_tests PRIVATE rm_core Qt6::Test)
add_test(NAME core COMMAND rm_tests)
