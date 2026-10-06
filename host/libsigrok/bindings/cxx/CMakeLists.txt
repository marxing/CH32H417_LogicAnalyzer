cmake_minimum_required(VERSION 3.10)
project(libsigrokcxx CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DHAVE_STOI_STOD)

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIBMM REQUIRED glibmm-2.4>=2.32.0)
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.32.0)

# Find libsigrok (installed or in-tree)
# First check if sigrok target exists (from top-level build)
if(TARGET sigrok)
    message(STATUS "Using in-tree libsigrok target")
    set(SIGROK_LIBS sigrok)
    set(SIGROK_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
        ${CMAKE_BINARY_DIR}/../libsigrok  # config.h output
    )
else()
    # Try pkg-config
    pkg_check_modules(LIBSIGROK libsigrok>=0.5.0)
    if(LIBSIGROK_FOUND)
        message(STATUS "Using system libsigrok via pkg-config")
        link_directories(${LIBSIGROK_LIBRARY_DIRS})
        set(SIGROK_LIBS ${LIBSIGROK_LIBRARIES})
        set(SIGROK_INCLUDE_DIRS ${LIBSIGROK_INCLUDE_DIRS})
    else()
        # Try find_library as last resort
        find_library(SIGROK_LIB sigrok
            PATHS ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_PREFIX}/bin /usr/lib /usr/local/lib
        )
        find_path(SIGROK_INCLUDE_DIR libsigrok/libsigrok.h
            PATHS ${CMAKE_INSTALL_PREFIX}/include /usr/include /usr/local/include
        )
        if(NOT SIGROK_LIB)
            message(FATAL_ERROR "libsigrok not found. Build libsigrok first.")
        endif()
        message(STATUS "Found libsigrok at ${SIGROK_LIB}")
        get_filename_component(SIGROK_LIBDIR ${SIGROK_LIB} DIRECTORY)
        set(SIGROK_LIBS ${SIGROK_LIB})
    endif()
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${SIGROK_INCLUDE_DIRS}
    ${GLIBMM_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
)

# Also add parent directory for generated config.h if using in-tree
if(TARGET sigrok)
    include_directories(${CMAKE_BINARY_DIR}/../libsigrok)
endif()

set(LIBSIGROKCXX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/classes.cpp
)

add_library(sigrokcxx SHARED ${LIBSIGROKCXX_SOURCES})

target_link_libraries(sigrokcxx
    ${SIGROK_LIBS}
    ${GLIBMM_LIBRARIES}
    ${GLIB_LIBRARIES}
)

# Ensure linker can find our installed libsigrok
link_directories(${CMAKE_INSTALL_PREFIX}/lib)
if(NOT TARGET sigrok AND LIBSIGROK_FOUND)
    link_directories(${LIBSIGROK_LIBRARY_DIRS})
endif()

set_target_properties(sigrokcxx PROPERTIES
    OUTPUT_NAME sigrokcxx
    VERSION 0.6.0
    SOVERSION 4
)

install(TARGETS sigrokcxx
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/libsigrokcxx/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libsigrokcxx
    FILES_MATCHING PATTERN "*.hpp"
)

# pkg-config file
set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix "\${prefix}")
set(libdir "\${exec_prefix}/lib")
set(includedir "\${prefix}/include")
set(SR_PACKAGE_VERSION "0.6.0-ch32h417")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/libsigrokcxx.pc.in ${CMAKE_CURRENT_BINARY_DIR}/libsigrokcxx.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libsigrokcxx.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
