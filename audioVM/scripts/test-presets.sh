#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/audiovm-presets.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT
cd "$project_dir"

# Keep a single wall-clock seed for this whole run so a failing fuzz case can
# be repeated by passing the printed value to the preset test binary.
random_seed="$(date +%s)"
printf 'Random seed: %s\n' "$random_seed"

"${CXX:-c++}" \
  -std=c++17 -O3 -ffast-math \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/tests/TestPresets.cpp" \
  -o "$temp_dir/test-presets"

"${CXX:-c++}" \
  -std=c++17 -O3 -ffast-math \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/tests/test_runtime_when.cpp" \
  -o "$temp_dir/test-runtime-when"

"${CXX:-c++}" \
  -std=c++17 -O3 -ffast-math \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/tests/test_op_definitions.cpp" \
  -o "$temp_dir/test-op-definitions"

"${CXX:-c++}" \
  -std=c++17 -O3 -ffast-math \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/tests/test_codegen.cpp" \
  -o "$temp_dir/test-codegen"

"$temp_dir/test-runtime-when"
"$temp_dir/test-op-definitions"
"$temp_dir/test-codegen"

presets=()
while IFS= read -r -d '' preset; do
  presets+=("$preset")
done < <(find web/presets -type f -name '*.py' -print0 | sort -z)

if ((${#presets[@]} == 0)); then
  echo "No preset files found in web/presets" >&2
  exit 1
fi

path_width=0
for preset in "${presets[@]}"; do
  ((${#preset} > path_width)) && path_width=${#preset}
done

for preset in "${presets[@]}"; do
  result="$(PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$project_dir/python" \
    python3 tests/compile_preset.py "$preset" \
    | "$temp_dir/test-presets" "$preset" "$random_seed")"
  IFS=$'\t' read -r controls peak realtime_factor <<< "$result"
  printf "%-${path_width}s  %2s controls  peak:%6.4f  RT-%%: %.4f\n" \
    "$preset" "$controls" "$peak" "$realtime_factor"
done
