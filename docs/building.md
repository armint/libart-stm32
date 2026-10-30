# Building and using libart-stm32

## Dependencies

The library needs these items:

- [libArT](https://github.com/armint/libart), CMake
  target `art::core`.
- [system-stm32](https://github.com/armint/system-stm32),
  CMake target `system-stm32`. system-stm32 also supplies the toolchain file
  (`cmake/gcc_arm.cmake`), the start-up code and the linker scripts.
- An `arm-none-eabi` GCC toolchain and CMake 3.20 or later.
- At C++17 only: span-compat. libArT and system-stm32 fetch it (see
  [Language standard](#language-standard)).

## Add the library to a firmware project

Put libArT, system-stm32 and libart-stm32 in the source tree of the firmware
project, for example as git submodules in `dependencies/`. Add libArT and
system-stm32 before this library, because this library links their targets.
Then link your executable with `art::stm32`:

```cmake
cmake_minimum_required(VERSION 3.20)
# Before project(): the toolchain file reads STM32_FAMILY.
set(STM32_FAMILY STM32G0 CACHE STRING "")
set(STM32_DEVICE STM32G071xx CACHE STRING "")
project(my-firmware C CXX ASM)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(dependencies/libArT)
add_subdirectory(dependencies/system-stm32)
add_subdirectory(dependencies/libart-stm32)

add_executable(firmware.elf src/main.cpp)
target_link_libraries(firmware.elf PRIVATE art::stm32)
```

Configure with the ARM toolchain file of system-stm32 and a build type:

```sh
cmake -B build \
    -D CMAKE_TOOLCHAIN_FILE=dependencies/system-stm32/cmake/gcc_arm.cmake \
    -D GCC_TOOLCHAIN_PREFIX=/opt/arm-gnu-toolchain \
    -D CMAKE_BUILD_TYPE=Release
```

`STM32_FAMILY` and `STM32_DEVICE` are system-stm32 variables. Set them as
cache entries before `project()`, because the toolchain file uses
`STM32_FAMILY` to select the core flags (`-mcpu=...`). If you set them after
`project()`, the build gets the core flags of the default family, and CMake
does not report it. Without a build type, the compiler gets no core flags
and the build fails. The system-stm32
[build documentation](https://github.com/armint/system-stm32/blob/main/docs/building.md)
gives the supported values and the other settings, for example the linker
scripts.
`ci/CMakeLists.txt` is a full example of such a superproject.

If the superproject defines an interface target `art-build-config`, the
library compiles with its compile options and definitions. Use it to give
all libraries the same flags, for example `-fno-exceptions`. Define the
target before the `add_subdirectory()` calls. Each library looks for the
target when CMake adds it, and ignores a target that you define later.

## Language standard

The minimum standard is C++17. The firmware projects use C++23. The library
does not select a standard. The consumer sets it with `CMAKE_CXX_STANDARD`.

The standard selects the types of `art::expected` and `art::span` (libArT,
`utils/compat/`) through the libArT option `ART_USE_STD_EXPECTED_SPAN`:

| Standard | `ART_USE_STD_EXPECTED_SPAN` | `art::expected` | `art::span` and `hw::span` |
|---|---|---|---|
| C++23 | `ON` (1), the default | `std::expected` | `std::span` |
| C++17 | `OFF` (0) | libArT's own expected | `span_compat::span` |

At C++17, the library needs [span-compat](https://github.com/armint/span-compat).
It gets span-compat through libArT (`art::core`) and system-stm32. This
library has no CMake setting for span-compat. span-compat must use its own
span (`SPAN_COMPAT_USE_STD_SPAN=OFF`). libArT and system-stm32 set this value
when they fetch span-compat.

All libraries in one build must use the same `ART_USE_STD_EXPECTED_SPAN`
value. This applies to libArT, libArT-proto, system-stm32, this library and
the application. `Readable` and `Writable` have virtual functions that use
these types. Thus a mixed build violates the one-definition rule, and the
compiler does not report it. `art-core` exports the value as a public compile
definition. Do not define it in a source file.

system-stm32 selects `hw::span` from `CMAKE_CXX_STANDARD`, not from
`ART_USE_STD_EXPECTED_SPAN`. Thus do not use C++20 or C++23 with
`ART_USE_STD_EXPECTED_SPAN=OFF`. Always set `CMAKE_CXX_STANDARD`. Otherwise,
`art::span` and `hw::span` can be different types. `Stm32UsbDeviceController.h`
has a `static_assert` that stops such a build.
