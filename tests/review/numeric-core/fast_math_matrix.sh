#!/usr/bin/env bash
# Review helper (numeric-core): compile FastMathProbe.cpp under each floating
# option; report whether the header guard fired and, if not, the runtime result.
# Usage (repo root, WSL): bash tests/review/numeric-core/fast_math_matrix.sh <cxx> <outdir>
cxx=$1; out=$2; mkdir -p "$out"
opts=("" "-ffast-math" "-Ofast" "-ffinite-math-only" "-fno-honor-nans" "-fno-honor-infinities"
      "-ffp-model=fast" "-ffp-model=aggressive" "-funsafe-math-optimizations" "-fno-signed-zeros"
      "-freciprocal-math" "-ffast-math -fno-finite-math-only" "-O2 -ffinite-math-only -fno-finite-math-only")
i=0
for opt in "${opts[@]}"; do
  i=$((i+1)); exe="$out/fm-$i"
  if $cxx -std=c++17 -O2 -Wall -Wextra -pedantic-errors -Ilib/telemetry -Ilib/delegate $opt \
       tests/review/numeric-core/FastMathProbe.cpp -o "$exe" > "$exe.log" 2>&1; then
    run=$("$exe" 2>&1 | tr '\n' ' ')
    printf '%-45s compiled; %s\n' "[$opt]" "$run"
  else
    if grep -q "Compile telemetry conversions without" "$exe.log"; then
      printf '%-45s guard rejected\n' "[$opt]"
    else
      printf '%-45s compile failed otherwise: %s\n' "[$opt]" "$(grep -m1 -i 'error' "$exe.log")"
    fi
  fi
done
