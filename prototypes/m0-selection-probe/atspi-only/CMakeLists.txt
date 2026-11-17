# AT-SPI measurement binary — built with the SYSTEM compiler on purpose.
#
# A conda/GCC cross toolchain produces a binary that requires a newer glibc than the desktop has, and
# its RUNPATH makes the process load conda libdbus/libglib while libatspi comes from the system. That
# mixing is what made atspi_init() fail in the first real run, so this target pins the system compiler
# unless the caller explicitly overrides it.
cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_CXX_COMPILER AND EXISTS "/usr/bin/c++")
    set(CMAKE_CXX_COMPILER "/usr/bin/c++")
endif()

# No RPATH at all. An activated conda environment exports LIBRARY_PATH, CMake then records those
# directories as an RPATH, and the binary ends up loading conda's libdbus/libglib while libatspi comes
# from the system — the exact mixing this target exists to avoid.
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_SKIP_BUILD_RPATH TRUE)

project(snip_atspi_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_path(ATSPI_INCLUDE_DIR NAMES atspi/atspi.h HINTS /usr/include/at-spi-2.0)
find_library(ATSPI_LIB NAMES atspi-2 atspi HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib)
find_path(GLIB_INCLUDE_DIR NAMES glib.h HINTS /usr/include/glib-2.0)
find_path(GLIB_CONFIG_DIR NAMES glibconfig.h
          HINTS /usr/lib/x86_64-linux-gnu/glib-2.0/include /usr/lib64/glib-2.0/include
                /usr/lib/glib-2.0/include)
find_library(GLIB_LIB NAMES glib-2.0 HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib)
find_library(GOBJECT_LIB NAMES gobject-2.0 HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib)
find_library(GIO_LIB NAMES gio-2.0 HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib)
# libatspi's public headers include dbus/dbus.h and dbus-arch-deps.h; we never call libdbus directly.
find_library(DBUS_LIB NAMES dbus-1 HINTS /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib)
find_path(DBUS_INCLUDE_DIR NAMES dbus/dbus.h HINTS /usr/include/dbus-1.0)
find_path(DBUS_ARCH_INCLUDE_DIR NAMES dbus/dbus-arch-deps.h
          HINTS /usr/lib/x86_64-linux-gnu/dbus-1.0/include /usr/lib64/dbus-1.0/include
                /usr/lib/dbus-1.0/include)

foreach(_req ATSPI_INCLUDE_DIR ATSPI_LIB GLIB_INCLUDE_DIR GLIB_CONFIG_DIR GLIB_LIB GOBJECT_LIB GIO_LIB
              DBUS_INCLUDE_DIR DBUS_ARCH_INCLUDE_DIR DBUS_LIB)
    if(NOT ${_req})
        message(FATAL_ERROR "Missing ${_req}. Install libatspi2.0-dev and libglib2.0-dev.")
    endif()
endforeach()

message(STATUS "compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "libatspi: ${ATSPI_LIB}")

add_executable(snip-atspi-probe ../src/atspi_plain.cpp)
target_include_directories(snip-atspi-probe PRIVATE ${ATSPI_INCLUDE_DIR} ${GLIB_INCLUDE_DIR}
                                                    ${GLIB_CONFIG_DIR} ${DBUS_INCLUDE_DIR}
                                                    ${DBUS_ARCH_INCLUDE_DIR})
target_link_libraries(snip-atspi-probe PRIVATE ${ATSPI_LIB} ${GLIB_LIB} ${GOBJECT_LIB} ${GIO_LIB}
                                             ${DBUS_LIB})
target_compile_options(snip-atspi-probe PRIVATE -Wall -Wextra -Wpedantic)
