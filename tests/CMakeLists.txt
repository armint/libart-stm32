cmake_minimum_required(VERSION 3.20)
Include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
)
set(DOCTEST_NO_INSTALL ON)
FetchContent_MakeAvailable(doctest)

# Header-only. libArT's usb::CdcAcmFunction keeps its queues in
# etl::circular_buffer. Same tag as libArT.
FetchContent_Declare(
    etl
    GIT_REPOSITORY https://github.com/ETLCPP/etl.git
    GIT_TAG 20.41.7
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(etl)

if (NOT DEFINED PROJECT_NAME)
    project(art-stm32-tests C CXX)
endif ()

# C++23 by default. CI also runs the tests at C++17 (-D CMAKE_CXX_STANDARD=17).
if (NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif ()

include(CTest)

# The USB tests use the real libArT USB stack (usb::Device,
# usb::CdcAcmFunction), Readable, and the fake controller of the libArT tests,
# from a libArT checkout next to this repository (as in a superproject, or as
# CI clones it). The stubs stay first on the include path, so they win over
# the libArT headers with the same name.
set(LIBART_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../libArT" CACHE PATH "libArT checkout for the host tests")
if (NOT EXISTS "${LIBART_DIR}/src/usb/DeviceController.h")
    message(FATAL_ERROR "libArT with USB support not found at LIBART_DIR=${LIBART_DIR}")
endif ()

file(GLOB TEST_SOURCES
    TestPowerManager.cpp
    TestSerialWritable.cpp
    TestStm32UsbDeviceController.cpp
    TestUsbSerial.cpp
)

add_executable(art-stm32-tests
    ${TEST_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/power/PowerManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/SerialWritable.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/usb/Stm32UsbDeviceController.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/usb/UsbSerial.cpp"
    "${LIBART_DIR}/src/usb/Device.cpp"
    "${LIBART_DIR}/src/usb/CdcAcmFunction.cpp"
    "${LIBART_DIR}/src/streams/Readable.cpp"
    "${LIBART_DIR}/src/utils/tfp_printf.cpp"
)

target_include_directories(art-stm32-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${LIBART_DIR}/src"
    "${LIBART_DIR}/tests/usb"
)

target_link_libraries(art-stm32-tests PRIVATE
    doctest::doctest
    etl::etl
)

# The tests compile the libArT sources directly, without the art-core target. So
# they set ART_USE_STD_EXPECTED_SPAN as art-core does: 1 (std::expected and
# std::span) at C++23, 0 (libArT's own expected and the span of span-compat)
# below. span-compat comes from the pin of libArT, with its own span
# (SPAN_COMPAT_USE_STD_SPAN=0).
if (CMAKE_CXX_STANDARD LESS 23)
    include("${LIBART_DIR}/cmake/span-compat.cmake")
    set(SPAN_COMPAT_USE_STD_SPAN OFF CACHE STRING "Span type of span-compat: AUTO, ON (std::span) or OFF (own)")
    FetchContent_Declare(
        span_compat
        GIT_REPOSITORY ${ART_SPAN_COMPAT_GIT_REPOSITORY}
        GIT_TAG ${ART_SPAN_COMPAT_GIT_TAG}
    )
    FetchContent_MakeAvailable(span_compat)
    target_link_libraries(art-stm32-tests PRIVATE span-compat::span-compat)
    target_compile_definitions(art-stm32-tests PRIVATE ART_USE_STD_EXPECTED_SPAN=0)
else ()
    target_compile_definitions(art-stm32-tests PRIVATE ART_USE_STD_EXPECTED_SPAN=1)
endif ()

add_test(NAME art-stm32-tests COMMAND art-stm32-tests)
