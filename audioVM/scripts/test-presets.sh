#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/audiovm-presets.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT
cd "$project_dir"

# Keep a single wall-clock seed for this whole run so a failing fuzz case can
# be repeated by passing the printed value to the preset test binary.
random_seed="$(date +%s)"
sample_rate=$((40000 + random_seed % 10001))
printf 'Random seed: %s\n' "$random_seed"
printf 'Sample rate: %s Hz\n' "$sample_rate"

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
done < <(find web/presets -maxdepth 1 -type f -name '*.py' -print0 | sort -z)

if ((${#presets[@]} == 0)); then
  echo "No preset files found in web/presets" >&2
  exit 1
fi

ops_total=0
controls_total=0
peak_total=0
rt_total=0
header_name=name
header_ops=ops
header_controls=controls
header_peak=peak
header_rt='RT (ppm)'
name_width=${#header_name}
ops_width=${#header_ops}
controls_width=${#header_controls}
peak_width=${#header_peak}
rt_width=${#header_rt}
average_name=average
((${#average_name} > name_width)) && name_width=${#average_name}
for preset in "${presets[@]}"; do
  preset_name="${preset#web/presets/}"
  ((${#preset_name} > name_width)) && name_width=${#preset_name}
done

printf "%-${name_width}s  %${ops_width}s  %${controls_width}s  %${peak_width}s  %${rt_width}s\n" \
  "$header_name" "$header_ops" "$header_controls" "$header_peak" "$header_rt"
for preset in "${presets[@]}"; do
  preset_name="${preset#web/presets/}"
  result="$(PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$project_dir/python" \
    python3 tests/compile_preset.py "$preset" \
    | "$temp_dir/test-presets" "$preset" "$random_seed" "$sample_rate")"
  IFS=$'\t' read -r ops controls peak realtime_factor <<< "$result"
  printf -v peak '%.4f' "$peak"
  printf -v realtime_factor '%.0f' "$realtime_factor"
  printf "%-${name_width}s  %${ops_width}s  %${controls_width}s  %${peak_width}s  %${rt_width}s\n" \
    "$preset_name" "$ops" "$controls" "$peak" "$realtime_factor"
  ops_total=$((ops_total + ops))
  controls_total=$((controls_total + controls))
  peak_total="$(awk -v total="$peak_total" -v value="$peak" 'BEGIN { print total + value }')"
  rt_total="$(awk -v total="$rt_total" -v value="$realtime_factor" 'BEGIN { print total + value }')"
done

average_ops="$(awk -v total="$ops_total" -v count="${#presets[@]}" 'BEGIN { printf "%.1f", total / count }')"
average_controls="$(awk -v total="$controls_total" -v count="${#presets[@]}" 'BEGIN { printf "%.1f", total / count }')"
average_peak="$(awk -v total="$peak_total" -v count="${#presets[@]}" 'BEGIN { printf "%.4f", total / count }')"
average_rt="$(awk -v total="$rt_total" -v count="${#presets[@]}" 'BEGIN { printf "%.0f", total / count }')"
printf "%-${name_width}s  %${ops_width}s  %${controls_width}s  %${peak_width}s  %${rt_width}s\n" \
  "$average_name" "$average_ops" "$average_controls" "$average_peak" "$average_rt"
