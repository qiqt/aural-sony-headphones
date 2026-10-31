cmake_minimum_required(VERSION 3.21)
project(Aural VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Threads REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Svg)

qt_standard_project_setup(REQUIRES 6.5)

# All QML lives in qml/ and is declared through QML_FILES, not a hand-written qmldir.
# QTP0004 (new in Qt 6.8) would want a qmldir in every such directory; keep the existing
# flat layout rather than churn the module structure for a policy warning. Guarded so the
# project still configures against Qt 6.5-6.7, where the policy does not exist.
if (QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 OLD)
endif()

# --- Build identity ------------------------------------------------------------------------
# A release candidate has to be traceable to the source it was built from (SON-17). Captured at
# configure time, so CMake is re-run whenever HEAD or the index moves - otherwise a commit made
# after configuring would leave the binary claiming the previous revision.
set(AURAL_GIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.git")
set(AURAL_BUILD_REVISION "unknown")
set(AURAL_BUILD_BRANCH "unknown")
set(AURAL_BUILD_MODIFIED 0)

find_package(Git QUIET)
if (GIT_FOUND AND EXISTS "${AURAL_GIT_DIR}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${AURAL_GIT_DIR}/HEAD" "${AURAL_GIT_DIR}/index")

    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short=10 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE AURAL_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE AURAL_BUILD_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE AURAL_GIT_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if (NOT AURAL_GIT_STATUS STREQUAL "")
        # Say so rather than let the revision imply the binary matches that commit.
        set(AURAL_BUILD_MODIFIED 1)
    endif()
    if (AURAL_BUILD_REVISION STREQUAL "")
        set(AURAL_BUILD_REVISION "unknown")
    endif()
endif()

message(STATUS "Aural build identity: ${PROJECT_VERSION} (${AURAL_BUILD_REVISION} on ${AURAL_BUILD_BRANCH}, modified=${AURAL_BUILD_MODIFIED})")

# Reuse the existing protocol/engine sources (no ImGui, no platform GUI).
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

qt_add_executable(appAural
    main.cpp
    backend/AppIcon.h
    backend/AppIcon.cpp
    backend/AppSettings.h
    backend/AppSettings.cpp
    backend/BuildInfo.h
    backend/BuildInfo.cpp
    backend/HeadphonesController.h
    backend/HeadphonesController.cpp
    backend/PlatformWindow.h
    backend/PlatformWindow.cpp
    backend/TrayController.h
    backend/TrayController.cpp
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

target_compile_definitions(appAural PRIVATE
    __HEADPHONES_APP_VERSION__="${PROJECT_VERSION}"
    AURAL_VERSION="${PROJECT_VERSION}"
    AURAL_BUILD_REVISION="${AURAL_BUILD_REVISION}"
    AURAL_BUILD_BRANCH="${AURAL_BUILD_BRANCH}"
    AURAL_BUILD_MODIFIED=${AURAL_BUILD_MODIFIED}
)

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
        qml/StatusChip.qml
        qml/DeviceToggleRow.qml
)

qt_add_resources(appAural "appfonts"
    PREFIX "/"
    FILES
        fonts/Manrope.ttf
        fonts/SpaceGrotesk.ttf
        fonts/MaterialIconsOutlined.otf
)

# Debug keeps a console so qDebug() is visible during bring-up; Release is a plain GUI app
# so a release candidate does not open a terminal behind the window.
set_target_properties(appAural PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

# Qt6::Widgets is needed for QSystemTrayIcon and its menu; the UI itself is still QML.
target_link_libraries(appAural PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg
    Threads::Threads
)

# --- Tests --------------------------------------------------------------------------------
# Controller-level tests (sessions, teardown, capability gating). On by default for a normal
# developer build and CI, including the release packaging job.
#
#   ctest --test-dir build/aural-debug -C Debug --output-on-failure
option(AURAL_BUILD_TESTS "Build the Qt controller tests" ON)
if (AURAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Packaging ----------------------------------------------------------------------------
# Stages a self-contained folder: the executable plus the Qt runtime, platform plugin and QML
# module it needs, so it runs on a machine with no Qt installed. Deliberately a staging
# directory and not an installer - producing and publishing a release is a separate decision.
#
#   cmake --build build/aural-release --config Release --target aural_package
#
# leaves the result in build/aural-release/package/Aural. See packaging/README.md.
#
# windeployqt is called directly rather than through qt_generate_deploy_qml_app_script():
# that helper writes the executable path into a generated script unquoted, so it breaks on
# any source tree whose path contains spaces (this one does). In a custom command CMake
# quotes the arguments for us.
if (WIN32)
    get_target_property(_aural_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_aural_qt_bin "${_aural_qmake_exe}" DIRECTORY)
    find_program(AURAL_WINDEPLOYQT NAMES windeployqt HINTS "${_aural_qt_bin}" REQUIRED)

    set(AURAL_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package/Aural")
    set(AURAL_REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

    # The MSVC runtime. windeployqt's --compiler-runtime only finds the redistributable when
    # VCINSTALLDIR / VCToolsRedistDir are set, i.e. from a Visual Studio developer prompt - a
    # plain shell silently gets nothing, which is how the folder ended up depending on the
    # user having the VC++ 2015-2022 redistributable installed. CMake already knows where the
    # DLLs are for the compiler it is using, so ask it instead. SKIP because we stage the
    # files ourselves rather than going through install().
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if (NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(WARNING "No MSVC runtime DLLs found: the staged folder will require the "
                        "Visual C++ 2015-2022 x64 redistributable on the target machine.")
    endif()

    # Says exactly what this folder is, so a bug report can name the source it came from.
    # Written per configuration because the build type is part of the answer.
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/BUILD-INFO-$<CONFIG>.txt" CONTENT
"Aural ${PROJECT_VERSION}
Source revision : ${AURAL_BUILD_REVISION}
Branch          : ${AURAL_BUILD_BRANCH}
Working tree    : $<IF:$<BOOL:${AURAL_BUILD_MODIFIED}>,modified (this build does not match that revision exactly),clean>
Configuration   : $<CONFIG>
Qt              : ${Qt6_VERSION}
Compiler        : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}
Target          : Windows x64

Aural is an independent, third-party utility. It is not affiliated with, endorsed by, or
sponsored by Sony. See THIRD-PARTY-NOTICES.md for the components bundled in this folder.
")

    add_custom_target(aural_package
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${AURAL_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AURAL_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:appAural>" "${AURAL_PACKAGE_DIR}/"
        COMMAND "${AURAL_WINDEPLOYQT}"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                --no-translations
                "${AURAL_PACKAGE_DIR}/$<TARGET_FILE_NAME:appAural>"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/BUILD-INFO-$<CONFIG>.txt" "${AURAL_PACKAGE_DIR}/BUILD-INFO.txt"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${AURAL_REPO_DIR}/LICENSE" "${AURAL_PACKAGE_DIR}/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${AURAL_REPO_DIR}/packaging/THIRD-PARTY-NOTICES.md" "${AURAL_PACKAGE_DIR}/"
        COMMENT "Staging a self-contained Aural folder in ${AURAL_PACKAGE_DIR}"
        VERBATIM
    )
    if (CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET aural_package POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "${AURAL_PACKAGE_DIR}/"
            COMMENT "Staging the MSVC runtime"
            VERBATIM
        )
    endif()

    add_dependencies(aural_package appAural)
endif()
