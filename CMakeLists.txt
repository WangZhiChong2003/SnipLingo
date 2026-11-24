cmake_minimum_required(VERSION 3.21)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SNIPLINGO_VERSION LIMIT_COUNT 1)
if(NOT SNIPLINGO_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+$")
    message(FATAL_ERROR "VERSION must contain a major.minor.patch version")
endif()
project(SnipLingo VERSION "${SNIPLINGO_VERSION}" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.2.4 REQUIRED COMPONENTS Core Network DBus Gui Qml Quick Widgets LinguistTools)
find_package(PkgConfig REQUIRED)

include(CTest)
include(GNUInstallDirs)

# -----------------------------------------------------------------------------------------------
# Module targets. The dependency direction is enforced by what each target links:
# core <- settings <- (coordinator, provider, selection, ui — later milestones).
# -----------------------------------------------------------------------------------------------
add_library(sniplyngo_core STATIC
    src/core/delay.cpp
    src/core/error.cpp
    src/core/log.cpp
    src/core/paths.cpp
)
target_include_directories(sniplyngo_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(sniplyngo_core PUBLIC Qt6::Core)

add_library(sniplyngo_settings STATIC
    src/settings/settings.cpp
    src/settings/settings_store.cpp
)
target_link_libraries(sniplyngo_settings PUBLIC sniplyngo_core)

add_library(sniplyngo_network STATIC
    src/app/network.cpp
)
target_link_libraries(sniplyngo_network PUBLIC sniplyngo_settings Qt6::Network)

add_library(sniplyngo_selection STATIC
    src/selection/backend_selector.cpp
    src/platform/atspi/selection_reading.cpp
    src/platform/x11/x11_protocol.cpp
    src/selection/selection_backend.h   # Q_OBJECT interface: listed so AUTOMOC generates its metaobject
)
target_link_libraries(sniplyngo_selection PUBLIC sniplyngo_core)

add_library(sniplyngo_provider STATIC
    src/provider/provider.h             # Q_OBJECT interface: listed so AUTOMOC generates its metaobject
    src/provider/openai_provider.cpp
    src/provider/prompt_builder.cpp
    src/provider/simple_translate_provider.cpp
    src/provider/sse_parser.cpp
)
target_link_libraries(sniplyngo_provider PUBLIC sniplyngo_core sniplyngo_settings Qt6::Network)

add_library(sniplyngo_ui STATIC
    src/ui/result_controller.cpp
    src/ui/settings_controller.cpp
)
target_link_libraries(sniplyngo_ui PUBLIC sniplyngo_coordinator sniplyngo_network Qt6::Gui)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sniplyngo_ui PRIVATE ${SNIPLYNGO_WARNINGS})
endif()

add_library(sniplyngo_secrets STATIC
    src/secrets/secret_service_store.cpp
    src/secrets/secret_store.cpp
)
target_link_libraries(sniplyngo_secrets PUBLIC sniplyngo_core Qt6::DBus)

add_library(sniplyngo_app STATIC
    src/app/clipboard.cpp
    src/app/backend_factory.cpp
    src/app/capability_report.cpp
    src/app/cli.cpp
    src/app/probe.cpp
)
target_link_libraries(sniplyngo_app
    PUBLIC sniplyngo_core sniplyngo_settings sniplyngo_selection sniplyngo_secrets Qt6::DBus
           Qt6::Gui)

add_library(sniplyngo_coordinator STATIC
    src/coordinator/request_coordinator.cpp
)
target_link_libraries(sniplyngo_coordinator
    PUBLIC sniplyngo_core sniplyngo_settings sniplyngo_selection sniplyngo_provider)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(SNIPLYNGO_WARNINGS -Wall -Wextra -Wpedantic)
    foreach(_target sniplyngo_core sniplyngo_settings sniplyngo_selection sniplyngo_provider
                     sniplyngo_coordinator sniplyngo_secrets sniplyngo_app)
        target_compile_options(${_target} PRIVATE ${SNIPLYNGO_WARNINGS})
    endforeach()
endif()

# clang-format helper referenced by CONTRIBUTING.md; no-op when the tool is absent.
# CI checks every tracked C/C++ file with clang-format 18. Prefer that exact binary: clang-format 14 and
# 18 indent continuation lines of braced initializers and call arguments differently, so formatting
# with another version produces a diff that fails CI.
find_program(SNIPLYNGO_CLANG_FORMAT NAMES clang-format-18 clang-format)
if(SNIPLYNGO_CLANG_FORMAT)
    execute_process(COMMAND "${SNIPLYNGO_CLANG_FORMAT}" --version
        OUTPUT_VARIABLE SNIPLYNGO_CLANG_FORMAT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT SNIPLYNGO_CLANG_FORMAT_VERSION MATCHES "^clang-format version 18\\.")
        message(WARNING
            "${SNIPLYNGO_CLANG_FORMAT} reports '${SNIPLYNGO_CLANG_FORMAT_VERSION}', but CI checks "
            "with clang-format 18. Install clang-format-18 (apt) or 'pip install "
            "clang-format==18.1.8', then reconfigure.")
    endif()
    file(GLOB_RECURSE SNIPLYNGO_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c")
    add_custom_target(format
        COMMAND "${SNIPLYNGO_CLANG_FORMAT}" -i ${SNIPLYNGO_FORMAT_SOURCES}
        COMMENT "Formatting C/C++ sources with ${SNIPLYNGO_CLANG_FORMAT_VERSION}"
        VERBATIM)
    add_custom_target(format-check
        COMMAND "${SNIPLYNGO_CLANG_FORMAT}" --dry-run --Werror ${SNIPLYNGO_FORMAT_SOURCES}
        COMMENT "Checking C/C++ formatting with ${SNIPLYNGO_CLANG_FORMAT_VERSION}"
        VERBATIM)
endif()

# -----------------------------------------------------------------------------------------------
# Optional AT-SPI backend. Absent development files are not an error: the app reports the backend as
# unavailable for this build instead of failing to compile.
# -----------------------------------------------------------------------------------------------
find_path(SNIPLYNGO_ATSPI_INCLUDE_DIR NAMES atspi/atspi.h HINTS /usr/include/at-spi-2.0 /usr/local/include/at-spi-2.0)
find_library(SNIPLYNGO_ATSPI_LIB NAMES atspi-2 atspi HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib)
find_path(SNIPLYNGO_GLIB_INCLUDE_DIR NAMES glib.h HINTS /usr/include/glib-2.0 /usr/local/include/glib-2.0)
find_path(SNIPLYNGO_GLIB_CONFIG_DIR NAMES glibconfig.h
          HINTS /usr/lib/x86_64-linux-gnu/glib-2.0/include /usr/lib64/glib-2.0/include /usr/lib/glib-2.0/include)
find_path(SNIPLYNGO_DBUS_INCLUDE_DIR NAMES dbus/dbus.h HINTS /usr/include/dbus-1.0 /usr/local/include/dbus-1.0)
find_path(SNIPLYNGO_DBUS_ARCH_INCLUDE_DIR NAMES dbus/dbus-arch-deps.h
          HINTS /usr/lib/x86_64-linux-gnu/dbus-1.0/include /usr/lib64/dbus-1.0/include /usr/lib/dbus-1.0/include)
find_library(SNIPLYNGO_GLIB_LIB NAMES glib-2.0 HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib)
find_library(SNIPLYNGO_GOBJECT_LIB NAMES gobject-2.0 HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib)

if(SNIPLYNGO_ATSPI_INCLUDE_DIR AND SNIPLYNGO_ATSPI_LIB AND SNIPLYNGO_GLIB_INCLUDE_DIR
   AND SNIPLYNGO_GLIB_CONFIG_DIR AND SNIPLYNGO_DBUS_INCLUDE_DIR AND SNIPLYNGO_DBUS_ARCH_INCLUDE_DIR
   AND SNIPLYNGO_GLIB_LIB AND SNIPLYNGO_GOBJECT_LIB)
    add_library(sniplyngo_platform_atspi STATIC src/platform/atspi/atspi_backend.cpp)
    target_include_directories(sniplyngo_platform_atspi PRIVATE
        ${SNIPLYNGO_ATSPI_INCLUDE_DIR} ${SNIPLYNGO_GLIB_INCLUDE_DIR} ${SNIPLYNGO_GLIB_CONFIG_DIR}
        ${SNIPLYNGO_DBUS_INCLUDE_DIR} ${SNIPLYNGO_DBUS_ARCH_INCLUDE_DIR})
    target_link_libraries(sniplyngo_platform_atspi PUBLIC sniplyngo_selection Qt6::DBus
        ${SNIPLYNGO_ATSPI_LIB} ${SNIPLYNGO_GLIB_LIB} ${SNIPLYNGO_GOBJECT_LIB})
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sniplyngo_platform_atspi PRIVATE ${SNIPLYNGO_WARNINGS})
    endif()
    target_compile_definitions(sniplyngo_app PRIVATE SNIPLYNGO_HAVE_ATSPI)
    target_link_libraries(sniplyngo_app PUBLIC sniplyngo_platform_atspi)
    message(STATUS "AT-SPI backend: enabled (${SNIPLYNGO_ATSPI_LIB})")
else()
    message(STATUS "AT-SPI backend: disabled (install libatspi2.0-dev and libglib2.0-dev to enable)")
endif()

# Optional X11 backend (XFixes PRIMARY). Absent development files are not an error.
pkg_check_modules(XCB_BACKEND QUIET IMPORTED_TARGET xcb xcb-xfixes)
if(XCB_BACKEND_FOUND)
    add_library(sniplyngo_platform_x11 STATIC src/platform/x11/x11_backend.cpp)
    target_link_libraries(sniplyngo_platform_x11
        PUBLIC sniplyngo_selection PkgConfig::XCB_BACKEND PRIVATE Qt6::DBus)
    pkg_check_modules(XCB_OUTSIDE_CLICK QUIET IMPORTED_TARGET xcb-xinput)
    if(XCB_OUTSIDE_CLICK_FOUND)
        target_sources(sniplyngo_platform_x11 PRIVATE src/platform/x11/x11_outside_click.cpp)
        target_link_libraries(sniplyngo_platform_x11 PRIVATE PkgConfig::XCB_OUTSIDE_CLICK Qt6::Gui)
    endif()
    target_compile_definitions(sniplyngo_app PUBLIC SNIPLYNGO_HAVE_X11)
    target_link_libraries(sniplyngo_app PUBLIC sniplyngo_platform_x11)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(sniplyngo_platform_x11 PRIVATE ${SNIPLYNGO_WARNINGS})
    endif()
    message(STATUS "X11 backend: enabled")
else()
    message(STATUS "X11 backend: disabled (install libxcb-xfixes0-dev to enable)")
endif()

# Runtime: the only target that pulls the UI, the coordinator and the provider together.
add_library(sniplyngo_runtime STATIC
    src/app/engine_cli.cpp
    src/app/provider_factory.cpp
    src/app/runtime.cpp
    src/app/swappable_provider.cpp
    src/ui/system_tray.cpp
)
target_link_libraries(sniplyngo_runtime
    PUBLIC sniplyngo_app sniplyngo_coordinator sniplyngo_ui sniplyngo_provider sniplyngo_network
           Qt6::Quick Qt6::Qml
    PRIVATE Qt6::Widgets)
if(XCB_BACKEND_FOUND AND XCB_OUTSIDE_CLICK_FOUND)
    target_compile_definitions(sniplyngo_runtime PRIVATE SNIPLYNGO_HAVE_X11_OUTSIDE_CLICK)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sniplyngo_runtime PRIVATE ${SNIPLYNGO_WARNINGS})
endif()

# The QML files ship inside the binary: the application must not depend on its source tree.
qt_add_resources(sniplyngo_runtime "ui_qml"
    PREFIX "/ui"
    BASE "src/ui"
    FILES
        src/ui/qml/Theme.qml
        src/ui/qml/AppButton.qml
        src/ui/qml/AppTextField.qml
        src/ui/qml/AppComboBox.qml
        src/ui/qml/AppScrollBar.qml
        src/ui/qml/AppScrollView.qml
        src/ui/qml/WindowButton.qml
        src/ui/qml/LanguageNames.js
        src/ui/qml/ResultCard.qml
        src/ui/qml/ResultWindow.qml
        src/ui/qml/EngineCatalog.qml
        src/ui/qml/AboutPage.qml
        src/ui/qml/SettingsWindow.qml
        src/ui/qml/ConsentSheet.qml)

# The Chinese catalogue is compiled at build time and embedded under :/i18n. Keeping the source
# strings in QML/C++ (qsTr/tr) means untranslated entries fall back to English.
qt_add_lrelease(sniplyngo_runtime
    TS_FILES translations/sniplyngo_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE sniplyngo_qm_files)
set_source_files_properties(${sniplyngo_qm_files} PROPERTIES GENERATED TRUE)
qt_add_resources(sniplyngo_runtime "sniplyngo_runtime_translations"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${sniplyngo_qm_files})
# Rewrites the catalogue from tr()/qsTr()/qsTranslate() sources. Run it after adding a string, then
# fill in the new entries in translations/sniplyngo_zh_CN.ts.
add_custom_target(update_translations
    COMMAND Qt6::lupdate
        -I "${CMAKE_CURRENT_SOURCE_DIR}/src"
        -locations relative
        -no-obsolete
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        -ts "${CMAKE_CURRENT_SOURCE_DIR}/translations/sniplyngo_zh_CN.ts"
    COMMENT "Updating translations/sniplyngo_zh_CN.ts"
    VERBATIM)

add_executable(sniplyngo src/main.cpp)
qt_add_resources(sniplyngo "branding"
    PREFIX "/branding"
    BASE "assets/branding"
    FILES assets/branding/sniplingo-icon.png)
qt_add_resources(sniplyngo "icons"
    PREFIX "/icons"
    BASE "assets/icons"
    FILES assets/icons/pin.svg assets/icons/pin-filled.svg)
if(TARGET sniplyngo_platform_atspi)
    target_compile_definitions(sniplyngo PRIVATE SNIPLYNGO_HAVE_ATSPI)
endif()
target_link_libraries(sniplyngo PRIVATE sniplyngo_runtime)
target_link_libraries(sniplyngo PRIVATE sniplyngo_app Qt6::Widgets)
target_compile_definitions(sniplyngo PRIVATE SNIPLYNGO_VERSION="${PROJECT_VERSION}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sniplyngo PRIVATE ${SNIPLYNGO_WARNINGS})
endif()

install(TARGETS sniplyngo RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES assets/sniplingo.desktop DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
install(FILES assets/branding/sniplingo-icon.png
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/512x512/apps")
install(FILES assets/branding/sniplingo-icon.svg
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps")
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/sniplingo" RENAME copyright)
install(FILES assets/branding/sniplingo-logo.png assets/branding/sniplingo-logo.svg
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/sniplingo/branding")
install(FILES docs/assets/sniplingo-architecture.png docs/assets/sniplingo-architecture.svg
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/sniplingo/architecture")

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
