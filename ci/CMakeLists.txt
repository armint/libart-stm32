cmake_minimum_required(VERSION 3.20)

# CI superproject: cross-builds art-stm32 for one STM32 family with libArT and
# system-stm32 next to this repository, as a firmware project does. The CI
# jobs (.gitlab-ci.yml) select the family (STM32_FAMILY, STM32_DEVICE) and the
# standard (CMAKE_CXX_STANDARD). libArT sets ART_USE_STD_EXPECTED_SPAN from
# the standard: ON at C++23, OFF below.
project(art-stm32-ci C CXX ASM)

set(LIBART_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../libArT" CACHE PATH "libArT checkout")
set(SYSTEM_STM32_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../system-stm32" CACHE PATH "system-stm32 checkout")

# The compile flags of a firmware build (as in universal-oven-controller).
add_library(art-build-config INTERFACE)
target_compile_definitions(art-build-config INTERFACE LIBART_ENABLE_FLOAT LIBART_ENABLE_64BIT RB_NO_STD_ATOMIC)
target_compile_options(art-build-config INTERFACE
        -Os -ffunction-sections -fdata-sections -fno-common -fsigned-char -Werror=odr
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-use-cxa-atexit>")

add_subdirectory(${LIBART_DIR} libArT)
add_subdirectory(${SYSTEM_STM32_DIR} system-stm32)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. libart-stm32)

# The sources of this repository must compile without warnings at every
# standard. The warning flags apply to art-stm32 only, not to libArT and
# system-stm32.
target_compile_options(art-stm32 PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Checks at compile time that art::span (libArT) and hw::span (system-stm32)
# are the span type of this standard (see SpanTypeCheck.cpp). It compiles
# with the usage requirements of art-stm32.
add_library(span-type-check OBJECT SpanTypeCheck.cpp)
target_link_libraries(span-type-check PRIVATE art-build-config art::core system-stm32)
target_include_directories(span-type-check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
