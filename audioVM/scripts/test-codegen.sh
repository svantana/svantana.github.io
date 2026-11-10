#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/audiovm-codegen.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT

"${CXX:-c++}" \
  -std=c++17 -O3 -ffast-math \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/tests/emit_codegen_fixture.cpp" \
  -o "$temp_dir/emit-codegen-fixture"

"$temp_dir/emit-codegen-fixture" > "$temp_dir/GeneratedGain.h"

"${CXX:-c++}" \
  -std=c++17 -O3 -ffast-math \
  -I"$project_dir/cpp" \
  -I"$temp_dir" \
  "$project_dir/tests/test_generated_operator.cpp" \
  -o "$temp_dir/test-generated-operator"

"$temp_dir/test-generated-operator"
