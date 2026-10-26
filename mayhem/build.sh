#!/usr/bin/env bash
#
# mayhem/build.sh — build SDL3's fuzz harnesses (sanitized, static libSDL3) and the oracle
# (normal-flags shared build + testautomation + KAT probe). Runs in the commit image, offline,
# and is safe to re-run. Build dirs are build/ and build-tests/ (both gitignored upstream).
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# CC/CXX reach cmake through the environment (exported above); passing -DCMAKE_C_COMPILER=clang
# makes cmake reset its cache on re-run, which breaks idempotency.
# 1) Static libSDL3, coverage-instrumented (and sanitized unless SANITIZER_FLAGS is empty; dummy
#    video/audio only: no X11/ALSA/... dev packages in the image). Four UBSan checks are relaxed for
#    the library only, because SDL's blitters and stb_image trip them on benign patterns for nearly
#    every input and would mask real defects: shift-base (signed left shift into the sign bit, e.g.
#    `Uint8 << 24`), float-cast-overflow (NaN/inf float pixels cast to Uint8), and pointer-overflow +
#    nonnull-attribute (stb_image's NULL + 0 and memcpy(NULL, ..., 0) on empty PNG/JPEG chunks).
#    ASan and the rest of UBSan stay halting. With an explicitly empty SANITIZER_FLAGS the library
#    gets neither the fuzzer-no-link coverage hooks (the -standalone links have no libFuzzer runtime
#    to resolve them) nor the -fno-sanitize relaxations.
LIB_SAN=""
[ -z "$SANITIZER_FLAGS" ] || LIB_SAN="-fsanitize=fuzzer-no-link -fno-sanitize=shift-base,float-cast-overflow,pointer-overflow,nonnull-attribute"
LIB_FLAGS="-O1 $SANITIZER_FLAGS $LIB_SAN $DEBUG_FLAGS"
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=None \
    -DCMAKE_C_FLAGS="$LIB_FLAGS" \
    -DCMAKE_CXX_FLAGS="$LIB_FLAGS" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_INSTALL=OFF -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_DEPS_SHARED=OFF
cmake --build build -j"$MAYHEM_JOBS"

# 2) Harnesses: fuzzer binary + run-once -standalone reproducer for each. LeakSanitizer is
#    turned off at build time via mayhem/lsan_off.cc.
HFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -I$SRC/include"
SDL_LIBS="$SRC/build/libSDL3.a -lm -ldl -lpthread"
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o /tmp/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o /tmp/standalone_main.o

build_harness() { # <name> <source> [extra cc flags]
    local name="$1" srcf="$2"; shift 2
    $CC $HFLAGS "$@" $LIB_FUZZING_ENGINE "$srcf" /tmp/lsan_off.o $SDL_LIBS -o "/mayhem/$name"
    $CC $HFLAGS "$@" "$srcf" /tmp/standalone_main.o /tmp/lsan_off.o $SDL_LIBS -o "/mayhem/$name-standalone"
}
build_harness fuzz_wav "$SRC/mayhem/fuzz_wav.c"
build_harness fuzz_bmp "$SRC/mayhem/fuzz_image.c" -DFUZZ_LOADER=SDL_LoadBMP_IO -DFUZZ_IS_BMP
build_harness fuzz_png "$SRC/mayhem/fuzz_image.c" -DFUZZ_LOADER=SDL_LoadPNG_IO -DFUZZ_IS_PNG
build_harness fuzz_jpg "$SRC/mayhem/fuzz_image.c" -DFUZZ_LOADER=SDL_LoadJPG_IO
build_harness fuzz_surface_ops "$SRC/mayhem/fuzz_surface_ops.c"
build_harness fuzz_audio_stream "$SRC/mayhem/fuzz_audio_stream.c"

# 3) Oracle: unsanitized shared libSDL3 + upstream testautomation + a KAT probe, normal flags.
cmake -S . -B build-tests -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=ON -DSDL_EXAMPLES=OFF \
    -DSDL_INSTALL=OFF -DSDL_UNIX_CONSOLE_BUILD=ON
cmake --build build-tests -j"$MAYHEM_JOBS" --target testautomation SDL3-shared
$CC -O1 $COVERAGE_FLAGS -I"$SRC/include" "$SRC/mayhem/kat_probe.c" \
    -L"$SRC/build-tests" -lSDL3 -Wl,-rpath,"$SRC/build-tests" -o "$SRC/build-tests/kat_probe"
