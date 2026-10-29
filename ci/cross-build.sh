#!/usr/bin/env bash
# Cross-builds art-stm32 for one STM32 family at one C++ standard through the
# CI superproject (ci/CMakeLists.txt), and checks that the build used the
# intended standard and span type. The CI jobs in .gitlab-ci.yml call it; it
# also runs locally.
#
# Environment:
#   STM32_FAMILY, STM32_DEVICE  family and CMSIS device, e.g. STM32L0 STM32L073xx
#   CXX_STANDARD                23 or 17
#   GCC_TOOLCHAIN_PREFIX        arm-none-eabi toolchain directory
#   LIBART_DIR, SYSTEM_STM32_DIR  optional, default: next to this repository
#   KNOWN_GAPS                  optional, space-separated sources under src/
#                               that fail with a known error (a family with
#                               open issues). Every other source of
#                               art-stm32 must compile. system-stm32 is then
#                               not a dependency of the build (see below).
#   KNOWN_GAP_PATTERN           with KNOWN_GAPS: extended regular expression.
#                               Every "error:" line of a failing known gap
#                               must match it. Another error fails the job.
#   BUILD_DIR                   optional, default: build-ci/<device>-cpp<std>
#   EXTRA_CMAKE_ARGS            optional, more configure arguments, e.g.
#                               -D FETCHCONTENT_SOURCE_DIR_SPAN_COMPAT=<dir>
set -euo pipefail

: "${STM32_FAMILY:?}" "${STM32_DEVICE:?}" "${CXX_STANDARD:?}" "${GCC_TOOLCHAIN_PREFIX:?}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIBART_DIR="${LIBART_DIR:-$ROOT/../libArT}"
SYSTEM_STM32_DIR="${SYSTEM_STM32_DIR:-$ROOT/../system-stm32}"
BUILD="${BUILD_DIR:-$ROOT/build-ci/$STM32_DEVICE-cpp$CXX_STANDARD}"

cmake -S "$ROOT/ci" -B "$BUILD" -G "Unix Makefiles" \
   -D CMAKE_TOOLCHAIN_FILE="$SYSTEM_STM32_DIR/cmake/gcc_arm.cmake" \
   -D GCC_TOOLCHAIN_PREFIX="$GCC_TOOLCHAIN_PREFIX" \
   -D CMAKE_BUILD_TYPE=Release \
   -D CMAKE_CXX_STANDARD="$CXX_STANDARD" \
   -D STM32_FAMILY="$STM32_FAMILY" \
   -D STM32_DEVICE="$STM32_DEVICE" \
   -D LIBART_DIR="$LIBART_DIR" \
   -D SYSTEM_STM32_DIR="$SYSTEM_STM32_DIR" \
   ${EXTRA_CMAKE_ARGS:-}

if [ -z "${KNOWN_GAPS:-}" ]; then
   cmake --build "$BUILD" --target art-stm32 span-type-check ci-images -j"$(nproc)"
else
   : "${KNOWN_GAP_PATTERN:?KNOWN_GAPS needs KNOWN_GAP_PATTERN}"
   status=0
   cmake --build "$BUILD" --target system-stm32 -j"$(nproc)" || { echo "FAIL: system-stm32 does not build"; status=1; }
   # The /fast targets compile without their dependencies, so the sources of
   # art-stm32 compile also when system-stm32 does not build.
   make -C "$BUILD" -k -j"$(nproc)" art-stm32/fast span-type-check/fast || true
   objects="$BUILD/libart-stm32/CMakeFiles/art-stm32.dir"
   checked=0
   while read -r source; do
      relative="${source#"$ROOT/"}"
      gap=0
      for known in $KNOWN_GAPS; do
         [ "src/$known" = "$relative" ] && gap=1
      done
      # CMakeLists.txt leaves out some sources for some families.
      if ! grep -qF "art-stm32.dir/$relative.obj:" "$objects/build.make"; then
         if [ "$gap" = 1 ]; then
            echo "FAIL: $relative is in KNOWN_GAPS, but the build leaves it out"
            status=1
         else
            echo "skipped: $relative is not part of the build for $STM32_FAMILY"
         fi
         continue
      fi
      checked=$((checked + 1))
      if [ -f "$objects/$relative.obj" ]; then
         [ "$gap" = 1 ] && echo "NOTE: $relative compiles now. Remove it from KNOWN_GAPS."
      elif [ "$gap" = 1 ]; then
         # Compile the file again alone, so that its errors are the only ones.
         log="$BUILD/known-gap-$(basename "$relative").log"
         make -C "$BUILD/libart-stm32" "${relative}.obj" > "$log" 2>&1 || true
         errors="$(grep 'error:' "$log" || true)"
         other="$(printf '%s\n' "$errors" | grep -v -E -- "$KNOWN_GAP_PATTERN" | grep . || true)"
         if [ -z "$errors" ]; then
            echo "FAIL: $relative does not compile, but its log has no error line"
            cat "$log"
            status=1
         elif [ -n "$other" ]; then
            echo "FAIL: $relative fails with errors other than the known gap:"
            printf '%s\n' "$other"
            status=1
         else
            echo "known gap: $relative fails with the known error only ($(printf '%s\n' "$errors" | wc -l) error lines)"
         fi
      else
         echo "FAIL: $relative does not compile and is not a known gap"
         status=1
      fi
   done < <(find "$ROOT/src" -name '*.cpp' | sort)
   [ "$checked" -gt 0 ] || { echo "FAIL: the build has none of the sources (format of $objects/build.make?)"; status=1; }
   [ -f "$BUILD/CMakeFiles/span-type-check.dir/SpanTypeCheck.cpp.obj" ] || { echo "FAIL: SpanTypeCheck.cpp does not compile"; status=1; }
   [ "$status" = 0 ] || exit 1
fi

# The standard and the span type of the build.
for flags in "$BUILD/libart-stm32/CMakeFiles/art-stm32.dir/flags.make" "$BUILD/CMakeFiles/span-type-check.dir/flags.make"; do
   grep -q -- "-std=gnu++$CXX_STANDARD" "$flags" || { echo "FAIL: $flags: not built at C++$CXX_STANDARD"; exit 1; }
   if [ "$CXX_STANDARD" -ge 23 ]; then
      grep -q "ART_USE_STD_EXPECTED_SPAN=1" "$flags" || { echo "FAIL: $flags: ART_USE_STD_EXPECTED_SPAN is not 1"; exit 1; }
      if grep -q "SYSTEM_STM32_USE_SPAN_COMPAT" "$flags"; then echo "FAIL: $flags: hw::span is not std::span"; exit 1; fi
   else
      grep -q "ART_USE_STD_EXPECTED_SPAN=0" "$flags" || { echo "FAIL: $flags: ART_USE_STD_EXPECTED_SPAN is not 0"; exit 1; }
      grep -q "SYSTEM_STM32_USE_SPAN_COMPAT" "$flags" || { echo "FAIL: $flags: hw::span is not span-compat"; exit 1; }
      grep -q "SPAN_COMPAT_USE_STD_SPAN=0" "$flags" || { echo "FAIL: $flags: span-compat does not use its own span"; exit 1; }
   fi
done
if [ "$CXX_STANDARD" -ge 23 ] && [ -e "$BUILD/_deps/span_compat-src" ]; then
   echo "FAIL: the C++23 build fetched span-compat"
   exit 1
fi
echo "OK: $STM32_DEVICE at C++$CXX_STANDARD"
