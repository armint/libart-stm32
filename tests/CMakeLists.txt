cmake_minimum_required(VERSION 3.20)
Include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
)
set(DOCTEST_NO_INSTALL ON)
FetchContent_MakeAvailable(doctest)

if (NOT DEFINED PROJECT_NAME)
    project(art-stm32-tests C CXX)
endif ()

set(CMAKE_CXX_STANDARD 23)

include(CTest)

file(GLOB TEST_SOURCES
    TestPowerManager.cpp
)

add_executable(art-stm32-tests
    ${TEST_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/power/PowerManager.cpp"
)

target_include_directories(art-stm32-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
)

target_link_libraries(art-stm32-tests PRIVATE
    doctest::doctest
)

add_test(NAME art-stm32-tests COMMAND art-stm32-tests)
