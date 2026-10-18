# Hardware test image for the NUCLEO-H533RE (see README.md in this directory).
# The CI superproject (ci/CMakeLists.txt) adds it for STM32H533xx. It uses the
# start-up code and the linker scripts of system-stm32, as the CDC echo image
# does.
set(_name hwtest-nucleo-h533)
add_executable(${_name}
        main.cpp
        Board.cpp
        Report.cpp
        TestsAdc.cpp
        TestsClock.cpp
        TestsDma.cpp
        TestsGpio.cpp
        TestsSerial.cpp
        TestsSpi.cpp
        TestsUsart.cpp
        TestsWatchdog.cpp
        Usb.cpp)
target_link_libraries(${_name} PRIVATE art-build-config art::core system-stm32 art-stm32)
target_compile_options(${_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(${_name} PRIVATE
    -nostartfiles
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--no-warn-rwx-segments
    -Wl,-Map=${_name}.map
    -L${SYSTEM_STM32_DIR}/ldscripts
    -L${SYSTEM_STM32_DIR}/tests/common
    -Tmem-H533xE.ld
    -Tlibs.ld
    -Tsections.ld)
add_custom_command(TARGET ${_name} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${_name}> ${_name}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${_name}>)
