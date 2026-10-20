# Controller-level tests: HeadphonesController driven by a real Qt event loop against
# FakeBluetoothConnector. Sessions, futures and teardown ordering live in the controller, so
# this is the only level they can be tested at (see test_controller.cpp).
#
# Qt6::Core only - no Gui, no Widgets, no QML. The controller is deliberately headless; the
# tray and window code is not under test here.

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Test)

set(ENGINE_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)

qt_add_executable(controller_tests
    test_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../backend/HeadphonesController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../backend/HeadphonesController.cpp
    ${ENGINE_TEST_DIR}/TestHarness.cpp
    ${ENGINE_DIR}/BluetoothWrapper.cpp
    ${ENGINE_DIR}/CommandSerializer.cpp
    ${ENGINE_DIR}/Headphones.cpp
    ${ENGINE_DIR}/Listener.cpp
)

# A console app: qt_add_executable defaults to WIN32_EXECUTABLE, which would swallow the output.
set_target_properties(controller_tests PROPERTIES WIN32_EXECUTABLE FALSE)

if (WIN32)
    target_sources(controller_tests PRIVATE ${ENGINE_DIR}/windows/WindowsBluetoothConnector.cpp)
    target_compile_definitions(controller_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
    target_link_libraries(controller_tests PRIVATE ws2_32 rpcrt4 Bthprops setupapi dwmapi)
endif()

target_compile_definitions(controller_tests PRIVATE __HEADPHONES_APP_VERSION__="tests")

target_include_directories(controller_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..     # backend/...
    ${ENGINE_DIR}
    ${ENGINE_TEST_DIR}                 # FakeBluetoothConnector.h, TestHarness.h
)

target_link_libraries(controller_tests PRIVATE Qt6::Core Qt6::Test Threads::Threads)

add_test(NAME controller COMMAND controller_tests)
set_tests_properties(controller PROPERTIES TIMEOUT 180)

if (WIN32)
    # ctest launches the binary with the ambient environment, which on a developer machine
    # does not have Qt on PATH - the run then dies with 0xc0000135 (DLL not found) before it
    # reaches a single test. Prepend Qt's bin directory for this test only.
    # ENVIRONMENT_MODIFICATION needs CMake 3.22; the project already requires 3.21, so bump
    # the check rather than silently doing nothing on an older one.
    if (CMAKE_VERSION VERSION_LESS 3.22)
        message(FATAL_ERROR "Building the controller tests needs CMake 3.22+ (ENVIRONMENT_MODIFICATION). "
                            "Configure with -DAURAL_BUILD_TESTS=OFF to skip them.")
    endif()
    set_tests_properties(controller PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
