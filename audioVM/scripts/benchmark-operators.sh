#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/audiovm-benchmark.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT

"${CXX:-c++}" \
  -std=c++17 -O3 -ffast-math \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/tests/BenchmarkOperators.cpp" \
  -o "$build_dir/benchmark-operators"

"$build_dir/benchmark-operators"
