cmake_minimum_required(VERSION 3.21)
project(Aural VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Threads REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg)

qt_standard_project_setup(REQUIRES 6.5)

# Reuse the existing protocol/engine sources (no ImGui, no platform GUI).
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

qt_add_executable(appAural
    main.cpp
    backend/HeadphonesController.h
    backend/HeadphonesController.cpp
    ${ENGINE_DIR}/BluetoothWrapper.cpp
    ${ENGINE_DIR}/CommandSerializer.cpp
    ${ENGINE_DIR}/Listener.cpp
    ${ENGINE_DIR}/Headphones.cpp
)

if (WIN32)
    target_sources(appAural PRIVATE ${ENGINE_DIR}/windows/WindowsBluetoothConnector.cpp)
    target_compile_definitions(appAural PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
    target_link_libraries(appAural PRIVATE ws2_32 rpcrt4 Bthprops setupapi dwmapi)
endif()

target_compile_definitions(appAural PRIVATE __HEADPHONES_APP_VERSION__="2.1.0")

target_include_directories(appAural PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ENGINE_DIR}
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(appAural
    URI Aural
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/SegmentedControl.qml
        qml/PillSwitch.qml
        qml/AudioSlider.qml
        qml/AuralButton.qml
        qml/AuralCombo.qml
        qml/EqBand.qml
        qml/EqCurve.qml
        qml/NavItem.qml
        qml/Toast.qml
        qml/DiscoveryScreen.qml
        qml/AmbientScreen.qml
        qml/DashboardScreen.qml
        qml/EqualizerScreen.qml
        qml/SpatialScreen.qml
        qml/SpeakToChatScreen.qml
        qml/ConnectedDevicesScreen.qml
        qml/SystemScreen.qml
        qml/PlaceholderScreen.qml
        qml/AboutScreen.qml
)

qt_add_resources(appAural "appfonts"
    PREFIX "/"
    FILES
        fonts/Manrope.ttf
        fonts/SpaceGrotesk.ttf
        fonts/MaterialIconsOutlined.otf
)

# Keep a console window during bring-up so qDebug() is visible. Flip to TRUE for release.
set_target_properties(appAural PROPERTIES WIN32_EXECUTABLE FALSE)

target_link_libraries(appAural PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg
    Threads::Threads
)
