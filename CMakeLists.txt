cmake_minimum_required(VERSION 3.21)
project(SnipLingo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network DBus Gui Qml Quick)
find_package(PkgConfig REQUIRED)

include(CTest)

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
find_program(SNIPLYNGO_CLANG_FORMAT NAMES clang-format)
if(SNIPLYNGO_CLANG_FORMAT)
    file(GLOB_RECURSE SNIPLYNGO_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c")
    add_custom_target(format
        COMMAND "${SNIPLYNGO_CLANG_FORMAT}" -i ${SNIPLYNGO_FORMAT_SOURCES}
        COMMENT "Formatting C++ sources with clang-format"
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
    target_link_libraries(sniplyngo_platform_x11 PUBLIC sniplyngo_selection PkgConfig::XCB_BACKEND)
    target_compile_definitions(sniplyngo_app PRIVATE SNIPLYNGO_HAVE_X11)
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
)
target_link_libraries(sniplyngo_runtime
    PUBLIC sniplyngo_app sniplyngo_coordinator sniplyngo_ui sniplyngo_provider sniplyngo_network
           Qt6::Quick Qt6::Qml)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sniplyngo_runtime PRIVATE ${SNIPLYNGO_WARNINGS})
endif()

# The QML files ship inside the binary: the application must not depend on its source tree.
qt_add_resources(sniplyngo_runtime "ui_qml"
    PREFIX "/ui"
    BASE "src/ui"
    FILES
        src/ui/qml/Theme.qml
        src/ui/qml/ResultWindow.qml
        src/ui/qml/SettingsWindow.qml
        src/ui/qml/ConsentSheet.qml)

add_executable(sniplyngo src/main.cpp)
if(TARGET sniplyngo_platform_atspi)
    target_compile_definitions(sniplyngo PRIVATE SNIPLYNGO_HAVE_ATSPI)
endif()
target_link_libraries(sniplyngo PRIVATE sniplyngo_runtime)
target_link_libraries(sniplyngo PRIVATE sniplyngo_app)
target_compile_definitions(sniplyngo PRIVATE SNIPLYNGO_VERSION="${PROJECT_VERSION}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sniplyngo PRIVATE ${SNIPLYNGO_WARNINGS})
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
