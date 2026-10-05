cmake_minimum_required(VERSION 3.20)

project(art-stm32 VERSION 0.1.0 LANGUAGES C CXX ASM)

include(GNUInstallDirs)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()

# Sources / headers
file(GLOB_RECURSE PROJECT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# STM32F1 does not have the peripherals some sources need, so leave them out
# (STM32_FAMILY comes from system-stm32):
# - I2cMaster: uses the I2C v2 registers (TIMINGR, ISR, TXDR); F1 has I2C v1.
# - power/TicklessIdleHook: uses the RTC wake-up timer and Stop 2 mode.
#   PowerManager stays, because SerialWritable needs it for its SleepLock.
# - sensors/InternalTemperatureSensor: needs the system-stm32 Adc with
#   factory calibration values; F1 has no calibration values in flash.
if (STM32_FAMILY STREQUAL "STM32F1")
    list(REMOVE_ITEM PROJECT_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/I2cMaster.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/power/TicklessIdleHook.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/sensors/InternalTemperatureSensor.cpp"
    )
endif ()

file(GLOB_RECURSE PROJECT_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

add_library(art-stm32 STATIC ${PROJECT_SOURCES})
add_library(art::stm32 ALIAS art-stm32)

# The sources need C++17 only. The consumer selects the standard
# (CMAKE_CXX_STANDARD); art::core can raise it to C++23 (see README).
target_compile_features(art-stm32 PUBLIC cxx_std_17)

target_include_directories(
        art-stm32
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Link against libArT core and system-stm32.
# We keep third-party deps PRIVATE here to avoid exporting build-tree targets
# via our install(EXPORT ...) set. The installed package config will
# re-attach transitive requirements for consumers.
target_link_libraries(art-stm32
        PUBLIC
            art::core
            system-stm32
)

# If a central build configuration target exists in the superproject,
# link it so this library is built with the same flags/defines.
if (TARGET art-build-config)
    # Only affect this target's compilation in the build tree; do not appear in installed export
    target_link_libraries(art-stm32 PRIVATE $<BUILD_INTERFACE:art-build-config>)
endif ()

# Ensure the exported interface does not pull non-exported targets.
set_property(TARGET art-stm32 PROPERTY INTERFACE_LINK_LIBRARIES "")

# Installation
install(TARGETS art-stm32
        EXPORT art-stm32-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Install public headers. Prefer an include/ layout if present.
if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/include)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
else ()
    # As a fallback, install headers colocated with sources into the include root
    if (PROJECT_HEADERS)
        install(FILES ${PROJECT_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif ()
endif ()

install(EXPORT art-stm32-targets
        NAMESPACE art::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/art-stm32)

# Generate a Config file so consumers can `find_package(art-stm32)`
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/art-stm32ConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

configure_package_config_file(
        ${CMAKE_CURRENT_LIST_DIR}/cmake/art-stm32Config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/art-stm32Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/art-stm32)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/art-stm32Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/art-stm32ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/art-stm32)
