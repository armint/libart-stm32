# Tests and CI

## Host tests

`tests/` contains host tests with doctest. Stubs in `tests/stubs` replace the
hardware and some libArT classes. The USB tests use the real libArT USB stack
(`usb::Device`, `usb::CdcAcmFunction`), `Readable` and the fake controller of
the libArT tests (`tests/usb/FakeDeviceController.h`). They find libArT in
`../libArT`, next to this repository. CMake downloads doctest and ETL (the
Embedded Template Library). Set `LIBART_DIR` to use another libArT checkout:

```sh
cmake -S tests -B build-tests -D LIBART_DIR=/path/to/libArT
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The tests use C++23. Add `-D CMAKE_CXX_STANDARD=17` to run them at C++17.
Then CMake also downloads span-compat, at the version that libArT specifies.

## Continuous integration

Two CI systems run the same jobs. Each clones libArT and system-stm32 (branch
`main`) next to this repository:

| System | File | Runs on | Clones from | Arm GCC |
|---|---|---|---|---|
| GitHub Actions | `.github/workflows/ci.yml` | Each pull request and each push to `main` | github.com/armint | 15.3 (`ghcr.io/armint/firmware-build-image:v1.15.0`) |
| GitLab CI | `.gitlab-ci.yml` | The mirror of `main`, and branches that you push to GitLab | GitLab, with the CI job token | 14.3 (`firmware-builder:v1.14.0`) |

On GitHub, STM32C031xx is a cell of `cross-build` with the same known gap.
The jobs are:

- `unit-tests`: the host tests at C++23 and at C++17.
- `cross-build`: a build of `art-stm32` for STM32L073xx, STM32G071xx,
  STM32G0B1xx, STM32F103xB and STM32H533xx, at C++23 and at C++17. The
  sources of this repository must compile with
  `-Wall -Wextra -Wpedantic -Werror`. The STM32H533xx job at C++23 also
  builds the CDC echo image and the hardware test image (see
  [STM32H5](stm32h5.md)), and keeps both `.hex` files as an artifact
  (`nucleo-h533-images` on GitHub).
- `cross-build-c0`: the same build for STM32C031xx. One known gap prevents a
  full build, issue 19 on the GitLab of the maintainers, not public
  (`InternalTemperatureSensor.cpp`). On `STM32C0`, the build leaves out
  `power/TicklessIdleHook`, because STM32C0 has no RTC wake-up timer.
  `InternalTemperatureSensor.cpp` can fail only with the known errors
  (`KNOWN_GAP_PATTERN`). The job fails if this file
  fails with another error, or if another source of `art-stm32` does not
  compile, or if system-stm32 does not build. The job prints a note when the
  gap is fixed.

The CI pipeline uses the latest `main` commit of libArT and system-stm32. The
job log shows the commits that it used.

Each job checks that the build used the intended standard and span type.
`ci/SpanTypeCheck.cpp` checks the types at compile time.

`ci/cross-build.sh` runs a cross build locally, with libArT and system-stm32
next to this repository. The build directory is `build-ci/`:

```sh
STM32_FAMILY=STM32L0 STM32_DEVICE=STM32L073xx CXX_STANDARD=17 \
GCC_TOOLCHAIN_PREFIX=/opt/arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi \
ci/cross-build.sh
```

The script header describes the other variables.
