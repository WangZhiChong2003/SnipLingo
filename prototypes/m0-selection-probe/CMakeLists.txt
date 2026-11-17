# Standalone build for the M0 feasibility probe.
#
# Intentionally NOT wired into a repository-root CMakeLists.txt or CI: this is a disposable
# experiment (see docs/DESIGN.md §13, milestone M0), not application code.
cmake_minimum_required(VERSION 3.21)
project(sniplyngo_m0_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xfixes)

# ---------------------------------------------------------------------------------------------
# libatspi + its GLib/D-Bus headers.
#
# Prefer system installations and absolute library paths: conda-style toolchains commonly ship a
# broken glib .pc file (include directory that does not exist, no lib*.so dev symlink), and the
# system copy is the one libatspi.so is built against.
# ---------------------------------------------------------------------------------------------
find_path(PROBE_ATSPI_INCLUDE_DIR NAMES atspi/atspi.h
          HINTS /usr/include/at-spi-2.0 /usr/local/include/at-spi-2.0)
find_library(PROBE_ATSPI_LIB NAMES atspi-2 atspi
             HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /usr/local/lib)
find_path(PROBE_GLIB_INCLUDE_DIR NAMES glib.h HINTS /usr/include/glib-2.0 /usr/local/include/glib-2.0)
find_path(PROBE_GLIB_CONFIG_DIR NAMES glibconfig.h
          HINTS /usr/lib/x86_64-linux-gnu/glib-2.0/include /usr/lib64/glib-2.0/include
                /usr/lib/glib-2.0/include /usr/local/lib/glib-2.0/include)
find_path(PROBE_DBUS_INCLUDE_DIR NAMES dbus/dbus.h HINTS /usr/include/dbus-1.0 /usr/local/include/dbus-1.0)
find_library(PROBE_GLIB_LIB NAMES glib-2.0 HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /usr/local/lib)
find_library(PROBE_GOBJECT_LIB NAMES gobject-2.0
             HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /usr/local/lib)
# GIO provides the GLib-side D-Bus pre-flight that isolates a libdbus problem from a bus problem.
find_library(PROBE_GIO_LIB NAMES gio-2.0
             HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib /usr/local/lib)

set(PROBE_ATSPI_INCLUDES "")
set(PROBE_ATSPI_LIBS "")
if(PROBE_ATSPI_INCLUDE_DIR AND PROBE_ATSPI_LIB AND PROBE_GLIB_INCLUDE_DIR AND PROBE_GLIB_CONFIG_DIR
   AND PROBE_GLIB_LIB AND PROBE_GOBJECT_LIB AND PROBE_GIO_LIB)
    set(PROBE_ATSPI_INCLUDES ${PROBE_ATSPI_INCLUDE_DIR} ${PROBE_GLIB_INCLUDE_DIR}
                             ${PROBE_GLIB_CONFIG_DIR} ${PROBE_DBUS_INCLUDE_DIR})
    # dbus-arch-deps.h lives in an architecture-specific directory on multiarch systems.
    foreach(_cand /usr/lib/x86_64-linux-gnu/dbus-1.0/include
                   /usr/lib64/dbus-1.0/include
                   /usr/lib/dbus-1.0/include)
        if(IS_DIRECTORY "${_cand}")
            list(APPEND PROBE_ATSPI_INCLUDES "${_cand}")
        endif()
    endforeach()
    set(PROBE_ATSPI_LIBS ${PROBE_ATSPI_LIB} ${PROBE_GLIB_LIB} ${PROBE_GOBJECT_LIB} ${PROBE_GIO_LIB})
    message(STATUS "libatspi: enabled (${PROBE_ATSPI_LIB})")
else()
    message(WARNING "libatspi/glib development files not found: 'atspi' modes will report unavailable. "
                    "Install libatspi2.0-dev and libglib2.0-dev (Debian/Ubuntu) or "
                    "at-spi2-core-devel and glib2-devel (Fedora).")
endif()

add_executable(snip-probe
    src/main.cpp
    src/caps.cpp
    src/x11_probe.cpp
    src/x11_selftest.cpp
    src/atspi_dbus.cpp
)

target_link_libraries(snip-probe PRIVATE Qt6::Core Qt6::DBus PkgConfig::XCB)
target_compile_options(snip-probe PRIVATE -Wall -Wextra -Wpedantic)

# The AT-SPI probe is deliberately Qt-free: the first real run showed the Qt build pulling libdbus
# and libglib from a conda prefix while libatspi came from the system, and atspi_init() then failed.
# A measuring tool must not mix runtimes, so this target links only GLib/libatspi.
# The AT-SPI measurement binary is built separately with the SYSTEM compiler (see atspi-only/).
# Reason: the first real run showed a conda-toolchain build needing GLIBC_2.38 and loading conda
# libdbus/libglib while libatspi came from the system, which made atspi_init() fail.
message(STATUS "For AT-SPI measurements build atspi-only/ with the system compiler: "
               "cmake -S atspi-only -B ../../build/m0-atspi -DCMAKE_BUILD_TYPE=RelWithDebInfo")
