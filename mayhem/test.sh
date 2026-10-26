#!/usr/bin/env bash
#
# mayhem/test.sh — RUN SDL's oracle (built by mayhem/build.sh, normal flags, dummy drivers):
#   1. mayhem/kat_probe: known-answer checks (WAV/PNG/BMP decode, audio + pixel-format conversion)
#      whose exact output is compared here against values verified independently of SDL;
#   2. upstream's testautomation suites over the same code paths (parsed from its run summary).
# A missing runner, a missing run summary, or any mismatch is a failure. Nothing is compiled here.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

export SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy
BT="$SRC/build-tests"
passed=0 failed=0 skipped=0

# --- 1. KAT probe -----------------------------------------------------------------------------
# sample.wav: MS-ADPCM, 238 blocks x 1012 samples = 240856 S16 samples = 481712 bytes (fmt/data
# chunk arithmetic). sample.png pixels decoded by an independent zlib+PNG-filter script.
# rgb565 = per-channel truncation (r>>3, g>>2, b>>3). BMP of 3x2 ARGB8888 = 14 + 124 (V5 header) + 24.
EXPECTED='wav.format=0x8010 channels=1 freq=22050 len=481712
png.size=408x167
png(100,40)=2a2804ff
png(300,50)=740076ff
png(150,120)=243e22ff
audio.outlen=32 0.5000 0.5000 -0.5000 -0.5000 0.0000 0.0000 -1.0000 -1.0000
rgb565 pitch=8 p00=08aa p20=58aa p12=5bca
bmp.size=162 magic=BM
bmp(0,0)=0a1450ff
bmp(2,1)=5a78539b'
if [ ! -x "$BT/kat_probe" ]; then
  echo "test.sh: $BT/kat_probe missing (mayhem/build.sh should have built it)" >&2
  emit_ctrf "sdl-kat+testautomation" 0 1 0
  exit 1
fi
ACTUAL=$("$BT/kat_probe" "$SRC/test/sample.wav" "$SRC/test/sample.png" 2>&1)
while IFS= read -r want; do
  if printf '%s\n' "$ACTUAL" | grep -qxF -- "$want"; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1)); echo "KAT FAIL: expected line '$want'" >&2
  fi
done <<< "$EXPECTED"
[ "$failed" -eq 0 ] || { echo "--- kat_probe output ---" >&2; echo "$ACTUAL" >&2; }

# --- 2. upstream testautomation suites ----------------------------------------------------------
TA="$BT/test/testautomation"
if [ ! -x "$TA" ]; then
  echo "test.sh: $TA missing (mayhem/build.sh should have built it)" >&2
  emit_ctrf "sdl-kat+testautomation" "$passed" $((failed + 1)) 0
  exit 1
fi
for suite in Surface Pixels Rect Audio Stdlib IOStream Blending; do
  out=$(cd "$BT/test" && timeout 600 "$TA" --filter "$suite" 2>&1 | sed 's/\x1b\[[0-9;]*m//g')
  line=$(printf '%s\n' "$out" | grep -E 'Run Summary: Total=[0-9]+ Passed=[0-9]+ Failed=[0-9]+ Skipped=[0-9]+' | tail -1)
  if [ -z "$line" ]; then
    failed=$((failed + 1)); echo "suite $suite: no run summary" >&2; continue
  fi
  p=$(sed -E 's/.*Passed=([0-9]+).*/\1/' <<< "$line")
  f=$(sed -E 's/.*Failed=([0-9]+).*/\1/' <<< "$line")
  s=$(sed -E 's/.*Skipped=([0-9]+).*/\1/' <<< "$line")
  echo "suite $suite: passed=$p failed=$f skipped=$s"
  [ "$p" -gt 0 ] || failed=$((failed + 1))
  passed=$((passed + p)); failed=$((failed + f)); skipped=$((skipped + s))
done

emit_ctrf "sdl-kat+testautomation" "$passed" "$failed" "$skipped"
