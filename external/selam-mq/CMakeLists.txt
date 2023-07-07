
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  foreach(lang C CXX)
    if(NOT DEFINED CMAKE_${lang}_COMPILER_LAUNCHER AND NOT CMAKE_${lang}_COMPILER MATCHES ".*/ccache")
      message(STATUS "Enabling ccache for ${lang}")
      set(CMAKE_${lang}_COMPILER_LAUNCHER ${CCACHE_PROGRAM} CACHE STRING "")
    endif()
  endforeach()
endif()

cmake_minimum_required(VERSION 3.7)

# Has to be set before `project()`, and ignored on non-macos:
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.12 CACHE STRING "macOS deployment target (Apple clang only)")

project(libselammq
    VERSION 1.2.11
    LANGUAGES CXX C)

include(GNUInstallDirs)

message(STATUS "selammq v${PROJECT_VERSION}")

set(SELAMMQ_LIBVERSION 0)


if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(selammq_IS_TOPLEVEL_PROJECT TRUE)
else()
    set(selammq_IS_TOPLEVEL_PROJECT FALSE)
endif()


option(BUILD_SHARED_LIBS "Build shared libraries instead of static ones" ON)
set(selammq_INSTALL_DEFAULT OFF)
if(BUILD_SHARED_LIBS OR selammq_IS_TOPLEVEL_PROJECT)
    set(selammq_INSTALL_DEFAULT ON)
endif()
option(SELAMMQ_BUILD_TESTS "Building and perform selammq tests" ${selammq_IS_TOPLEVEL_PROJECT})
option(SELAMMQ_INSTALL "Add selammq libraries and headers to cmake install target; defaults to ON if BUILD_SHARED_LIBS is enabled or we are the top-level project; OFF for a static subdirectory build" ${selammq_INSTALL_DEFAULT})
option(SELAMMQ_INSTALL_CPPZMQ "Install cppzmq header with selammq/ headers (requires SELAMMQ_INSTALL)" ON)
option(SELAMMQ_LOKIMQ_COMPAT "Install lokimq compatibility headers and pkg-config for rename migration" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

configure_file(selammq/version.h.in selammq/version.h @ONLY)
configure_file(libselammq.pc.in libselammq.pc @ONLY)
if(SELAMMQ_LOKIMQ_COMPAT)
    configure_file(liblokimq.pc.in liblokimq.pc @ONLY)
endif()


add_library(selammq
    selammq/address.cpp
    selammq/auth.cpp
    selammq/connections.cpp
    selammq/jobs.cpp
    selammq/selammq.cpp
    selammq/proxy.cpp
    selammq/worker.cpp
)
set_target_properties(selammq PROPERTIES SOVERSION ${SELAMMQ_LIBVERSION})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(selammq PRIVATE Threads::Threads)


if(TARGET selamc)
    target_link_libraries(selammq PUBLIC selamc)
elseif(BUILD_SHARED_LIBS)
    include(FindPkgConfig)
    pkg_check_modules(selamc libselamc IMPORTED_TARGET)

    if(selamc_FOUND)
        target_link_libraries(selammq PUBLIC PkgConfig::selamc)
    else()
        add_subdirectory(selam-encoding)
        target_link_libraries(selammq PUBLIC selamc)
    endif()
else()
    add_subdirectory(selam-encoding)
    target_link_libraries(selammq PUBLIC selamc)
endif()

# libzmq is nearly impossible to link statically from a system-installed static library: it depends
# on a ton of other libraries, some of which are not all statically available.  If the caller wants
# to mess with this, so be it: they can set up a libzmq target and we'll use it.  Otherwise if they
# asked us to do things statically, don't even try to find a system lib and just build it.
set(selammq_build_static_libzmq OFF)
if(TARGET libzmq)
    target_link_libraries(selammq PUBLIC libzmq)
elseif(BUILD_SHARED_LIBS)
    include(FindPkgConfig)
    pkg_check_modules(libzmq libzmq>=4.3 IMPORTED_TARGET)

    if(libzmq_FOUND)
        # Debian sid includes a -isystem in the mit-krb package that, starting with pkg-config 0.29.2,
        # breaks cmake's pkgconfig module because it stupidly thinks "-isystem" is a path, so if we find
        # -isystem in the include dirs then hack it out.
        get_property(zmq_inc TARGET PkgConfig::libzmq PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
        list(FIND zmq_inc "-isystem" broken_isystem)
        if(NOT broken_isystem EQUAL -1)
          list(REMOVE_AT zmq_inc ${broken_isystem})
          set_property(TARGET PkgConfig::libzmq PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${zmq_inc})
        endif()

        target_link_libraries(selammq PUBLIC PkgConfig::libzmq)
    else()
        set(selammq_build_static_libzmq ON)
    endif()
else()
    set(selammq_build_static_libzmq ON)
endif()

if(selammq_build_static_libzmq)
    message(STATUS "libzmq >= 4.3 not found or static build requested, building bundled version")
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/local-libzmq")
    include(LocalLibzmq)
    target_link_libraries(selammq PUBLIC libzmq_vendor)
endif()

target_include_directories(selammq
    PUBLIC
        $<INSTALL_INTERFACE:>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cppzmq>
)

target_compile_options(selammq PRIVATE -Wall -Wextra)

option(WARNINGS_AS_ERRORS "treat all warnings as errors" ON)
if(WARNINGS_AS_ERRORS)
    target_compile_options(selammq PRIVATE -Werror)
endif()

target_compile_features(selammq PUBLIC cxx_std_17)
set_target_properties(selammq PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(link_dep_libs target linktype libdirs)
    foreach(lib ${ARGN})
        find_library(link_lib-${lib} NAMES ${lib} PATHS ${libdirs})
        if(link_lib-${lib})
            target_link_libraries(${target} ${linktype} ${link_lib-${lib}})
        endif()
    endforeach()
endfunction()

# If the caller has already set up a sodium target then we will just link to it, otherwise we go
# looking for it.
if(TARGET sodium)
    target_link_libraries(selammq PUBLIC sodium)
    if(selammq_build_static_libzmq)
        target_link_libraries(libzmq_vendor INTERFACE sodium)
    endif()
else()
    include(FindPkgConfig)
    pkg_check_modules(sodium REQUIRED libsodium IMPORTED_TARGET)

    if(BUILD_SHARED_LIBS)
        target_link_libraries(selammq PUBLIC PkgConfig::sodium)
        if(selammq_build_static_libzmq)
            target_link_libraries(libzmq_vendor INTERFACE PkgConfig::sodium)
        endif()
    else()
        link_dep_libs(selammq PUBLIC "${sodium_STATIC_LIBRARY_DIRS}" ${sodium_STATIC_LIBRARIES})
        target_include_directories(selammq PUBLIC ${sodium_STATIC_INCLUDE_DIRS})
        if(selammq_build_static_libzmq)
            link_dep_libs(libzmq_vendor INTERFACE "${sodium_STATIC_LIBRARY_DIRS}" ${sodium_STATIC_LIBRARIES})
            target_link_libraries(libzmq_vendor INTERFACE ${sodium_STATIC_INCLUDE_DIRS})
        endif()
    endif()
endif()

add_library(selammq::selammq ALIAS selammq)
if(SELAMMQ_LOKIMQ_COMPAT)
    add_library(lokimq::lokimq ALIAS selammq)
endif()

export(
    TARGETS selammq
    NAMESPACE selammq::
    FILE selammqTargets.cmake
)

if(SELAMMQ_INSTALL)
    install(
        TARGETS selammq
        EXPORT selammqConfig
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )

    install(
        FILES selammq/address.h
        selammq/auth.h
        selammq/base32z.h
        selammq/base64.h
        selammq/batch.h
        selammq/bt_serialize.h
        selammq/bt_value.h
        selammq/connections.h
        selammq/hex.h
        selammq/selammq.h
        selammq/message.h
        selammq/variant.h
        ${CMAKE_CURRENT_BINARY_DIR}/selammq/version.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/selammq
        )

    if(SELAMMQ_INSTALL_CPPZMQ)
        install(
            FILES cppzmq/zmq.hpp
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/selammq
            )
    endif()


    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/libselammq.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    )

    if(SELAMMQ_LOKIMQ_COMPAT)
        install(
            FILES lokimq/address.h
            lokimq/auth.h
            lokimq/base32z.h
            lokimq/base64.h
            lokimq/batch.h
            lokimq/bt_serialize.h
            lokimq/bt_value.h
            lokimq/connections.h
            lokimq/hex.h
            lokimq/lokimq.h
            lokimq/message.h
            lokimq/variant.h
            lokimq/version.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lokimq
            )

        install(
            FILES ${CMAKE_CURRENT_BINARY_DIR}/liblokimq.pc
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
            )
    endif()


endif()

if(SELAMMQ_BUILD_TESTS)
    add_subdirectory(tests)
endif()
