cmake_minimum_required(VERSION 3.21)
project(AuralEngineTests LANGUAGES CXX)

# Engine tests only: no Qt, no ImGui, no platform Bluetooth stack. Everything runs against
# FakeBluetoothConnector, so this configures and runs anywhere with a C++20 compiler.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(engine_tests
    TestHarness.cpp
    TestMain.cpp
    test_core.cpp
    test_transport.cpp
    test_ack.cpp
    test_settings.cpp
    test_lifecycle.cpp
    test_model.cpp
    test_protocol.cpp
    test_discovery.cpp
    test_readback.cpp
    test_toggles.cpp
    test_multipoint.cpp
    test_audio.cpp
    test_eqcap.cpp
    test_ambientcap.cpp
    test_v2ambient.cpp
    test_v2audio.cpp
    test_v2.cpp
    test_v2session.cpp
    test_assignment.cpp
    ${ENGINE_DIR}/BluetoothWrapper.cpp
    ${ENGINE_DIR}/CommandSerializer.cpp
    ${ENGINE_DIR}/Headphones.cpp
    ${ENGINE_DIR}/Listener.cpp
)

target_include_directories(engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ENGINE_DIR}
)

target_compile_definitions(engine_tests PRIVATE __HEADPHONES_APP_VERSION__="tests")

if (MSVC)
    # /W4 here would be all pre-existing noise from the protocol enums (deliberate
    # signed-char casts), so stay at the default level and keep warnings meaningful.
    target_compile_options(engine_tests PRIVATE /permissive-)
else()
    target_compile_options(engine_tests PRIVATE -Wall -Wextra)
endif()

target_link_libraries(engine_tests PRIVATE Threads::Threads)

enable_testing()

# One CTest entry per suite: a failure names the area rather than "the tests".
foreach(suite core transport ack settings lifecycle model protocol discovery readback toggles multipoint audio eqcap assignment ambientcap v2 v2session v2ambient v2audio)
    add_test(NAME ${suite} COMMAND engine_tests ${suite}_)
    set_tests_properties(${suite} PROPERTIES TIMEOUT 120)
endforeach()

if(WIN32)
    target_sources(engine_tests PRIVATE test_windows_socket.cpp)
    target_link_libraries(engine_tests PRIVATE ws2_32)
    target_compile_definitions(engine_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME windows_socket COMMAND engine_tests windows_socket_)
    set_tests_properties(windows_socket PROPERTIES TIMEOUT 30)
endif()
