cmake_minimum_required(VERSION 3.21)
project(AuralHeadsetProbe LANGUAGES CXX)
if(NOT WIN32)
    message(FATAL_ERROR "The hardware probe currently uses the Windows Bluetooth connector")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
find_package(Threads REQUIRED)
add_executable(headset_probe main.cpp
    ${ENGINE_DIR}/BluetoothWrapper.cpp
    ${ENGINE_DIR}/CommandSerializer.cpp
    ${ENGINE_DIR}/Headphones.cpp
    ${ENGINE_DIR}/Listener.cpp
    ${ENGINE_DIR}/windows/WindowsBluetoothConnector.cpp)
target_include_directories(headset_probe PRIVATE ${ENGINE_DIR})
target_compile_definitions(headset_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(headset_probe PRIVATE Threads::Threads ws2_32 rpcrt4 Bthprops setupapi)
