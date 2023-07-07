cmake_minimum_required(VERSION 3.10)  # bionic's cmake version

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Has to be set before `project()`, and ignored on non-macos:
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.12 CACHE STRING "macOS deployment target (Apple clang only)")

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  foreach(lang ${LANGS})
    if(NOT DEFINED CMAKE_${lang}_COMPILER_LAUNCHER AND NOT CMAKE_${lang}_COMPILER MATCHES ".*/ccache")
      message(STATUS "Enabling ccache for ${lang}")
      set(CMAKE_${lang}_COMPILER_LAUNCHER ${CCACHE_PROGRAM} CACHE STRING "")
    endif()
  endforeach()
endif()

project(selamc
    VERSION 1.0.3
    DESCRIPTION "selamc - Base 16/32/64 and Bittorrent Encoding/Decoding Header Only Library"
    LANGUAGES CXX)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

message(STATUS "selamc v${PROJECT_VERSION}")

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(selamc_IS_TOPLEVEL_PROJECT TRUE)
else()
    set(selamc_IS_TOPLEVEL_PROJECT FALSE)
endif()

option(SELAMC_BUILD_TESTS "Building and perform selamc tests" ${selamc_IS_TOPLEVEL_PROJECT})
option(SELAMC_BUILD_DOCS "Build selamc documentation" ${selamc_IS_TOPLEVEL_PROJECT})
option(SELAMC_INSTALL "Add selamc headers to install target" ${selamc_IS_TOPLEVEL_PROJECT})


configure_file(selamc/version.h.in selamc/version.h @ONLY)
configure_file(libselamc.pc.in libselamc.pc @ONLY)


add_library(selamc INTERFACE)

target_include_directories(selamc
    INTERFACE
        $<INSTALL_INTERFACE:>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

export(
    TARGETS selamc
    NAMESPACE selamc::
    FILE selamcTargets.cmake
)

if(SELAMC_INSTALL)
  install(
    TARGETS selamc
    EXPORT selamcConfig
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

  install(
    FILES
    selamc/base32z.h
    selamc/base64.h
    selamc/bt.h
    selamc/bt_producer.h
    selamc/bt_serialize.h
    selamc/bt_value.h
    selamc/byte_type.h
    selamc/endian.h
    selamc/hex.h
    selamc/variant.h
    ${CMAKE_CURRENT_BINARY_DIR}/selamc/version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/selamc
    )


  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/libselamc.pc
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pkgconfig
    )
endif()


if(SELAMC_BUILD_DOCS)
  add_subdirectory(docs)
endif()

if(SELAMC_BUILD_TESTS)
  add_subdirectory(tests)
endif()
