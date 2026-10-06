cmake_minimum_required(VERSION 3.10)
project(libsigrokdecode C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.32.0)
pkg_check_modules(PYTHON3 REQUIRED python3>=3.2)

# Platform
if(WIN32 OR MINGW)
    set(PLATFORM_LIBS ws2_32)
    set(CONF_HOST "x86_64-w64-mingw32")
    add_definitions(-DWIN32_LEAN_AND_MEAN)
add_definitions(-DDECODERS_DIR="${CMAKE_INSTALL_PREFIX}/share/libsigrokdecode/decoders")
else()
    set(PLATFORM_LIBS m pthread dl)
    set(CONF_HOST "${CMAKE_SYSTEM_PROCESSOR}-pc-linux-gnu")
endif()

# config.h generation
set(HAVE_DLFCN_H 0)
set(HAVE_INTTYPES_H 1)
set(HAVE_MEMORY_H 1)
set(HAVE_STDINT_H 1)
set(HAVE_STDLIB_H 1)
set(HAVE_STRINGS_H 1)
set(HAVE_STRING_H 1)
set(HAVE_SYS_STAT_H 1)
set(HAVE_SYS_TYPES_H 1)
set(HAVE_UNISTD_H 1)
set(PACKAGE_NAME "libsigrokdecode")
set(PACKAGE_STRING "libsigrokdecode 0.6.0")
set(PACKAGE_TARNAME "libsigrokdecode")
set(PACKAGE_URL "http://www.sigrok.org")
set(PACKAGE_VERSION "0.6.0")
set(PACKAGE_BUGREPORT "sigrok-devel@lists.sourceforge.net")
set(SRD_PACKAGE_VERSION_MAJOR 0)
set(SRD_PACKAGE_VERSION_MINOR 6)
set(SRD_PACKAGE_VERSION_MICRO 0)
set(SRD_PACKAGE_VERSION_STRING "0.6.0")
set(SRD_LIB_VERSION_CURRENT 4)
set(SRD_LIB_VERSION_REVISION 0)
set(SRD_LIB_VERSION_AGE 0)
set(SRD_LIB_VERSION_STRING "4:0:0")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

# version.h generation (using ifndef guards, no conflict)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/version.h
"#ifndef LIBSIGROKDECODE_VERSION_H\n#define LIBSIGROKDECODE_VERSION_H\n\
#define SRD_PACKAGE_VERSION_MAJOR 0\n\
#define SRD_PACKAGE_VERSION_MINOR 6\n\
#define SRD_PACKAGE_VERSION_MICRO 0\n\
#define SRD_PACKAGE_VERSION_STRING \"0.6.0\"\n\
#define SRD_LIB_VERSION_CURRENT 4\n\
#define SRD_LIB_VERSION_REVISION 0\n\
#define SRD_LIB_VERSION_AGE 0\n\
#define SRD_LIB_VERSION_STRING \"4:0:0\"\n\
#endif\n")

# Compiler flags
if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4996")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter")
    if(NOT DISABLE_WERROR)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
    endif()
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GLIB_INCLUDE_DIRS}
    ${PYTHON3_INCLUDE_DIRS}
)

# Source files
set(LIBSIGROKDECODE_SOURCES
    decoder.c
    error.c
    exception.c
    instance.c
    log.c
    module_sigrokdecode.c
    session.c
    srd.c
    type_decoder.c
    util.c
    version.c
)

# Library
add_library(sigrokdecode SHARED ${LIBSIGROKDECODE_SOURCES})

# Export all symbols on Windows (needed for DLL import lib)
if(WIN32 OR MINGW)
    set_target_properties(sigrokdecode PROPERTIES LINK_FLAGS "-Wl,--export-all-symbols")
endif()

target_link_libraries(sigrokdecode
    ${GLIB_LIBRARIES}
    ${PYTHON3_LIBRARIES}
    ${PLATFORM_LIBS}
)

set_target_properties(sigrokdecode PROPERTIES
    OUTPUT_NAME sigrokdecode
    VERSION 0.6.0
    SOVERSION 4
)

install(TARGETS sigrokdecode
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install headers
install(FILES libsigrokdecode.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libsigrokdecode
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/version.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libsigrokdecode
)

# Install decoders (Python protocol decoders)
install(DIRECTORY decoders/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/libsigrokdecode/decoders
    FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE
)

# pkg-config
set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix "\${prefix}")
set(libdir "\${exec_prefix}/lib")
set(includedir "\${prefix}/include")

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/libsigrokdecode.pc
"prefix=${prefix}
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: libsigrokdecode
Description: Protocol decoding library for sigrok
URL: http://www.sigrok.org
Version: 0.6.0
Requires: glib-2.0
Libs: -L\${libdir} -lsigrokdecode
Cflags: -I\${includedir}
")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libsigrokdecode.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
