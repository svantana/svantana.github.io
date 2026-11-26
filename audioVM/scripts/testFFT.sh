#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/audiovm-fft.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT

"${CXX:-c++}" -std=c++17 -O3 \
  "$project_dir/tests/testFFT.cpp" -o "$temp_dir/testFFT"

printf 'Portable FFT\n'
"$temp_dir/testFFT"

if [[ "$(uname -s)" == Darwin ]]; then
  "${CXX:-c++}" -std=c++17 -O3 -DAUDIOVM_FFT_USE_VDSP \
    "$project_dir/tests/testFFT.cpp" -framework Accelerate -o "$temp_dir/testFFT-vDSP"
  printf '\nvDSP FFT\n'
  "$temp_dir/testFFT-vDSP"
fi
