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
done < <(find web/presets -type f -name '*.py' -print0 | sort -z)

if ((${#presets[@]} == 0)); then
  echo "No preset files found in web/presets" >&2
  exit 1
fi

rows=()
header_name=name
header_ops=ops
header_controls=controls
header_peak=peak
header_rt=RT
name_width=${#header_name}
ops_width=${#header_ops}
controls_width=${#header_controls}
peak_width=${#header_peak}
rt_width=${#header_rt}
for preset in "${presets[@]}"; do
  result="$(PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$project_dir/python" \
    python3 tests/compile_preset.py "$preset" \
    | "$temp_dir/test-presets" "$preset" "$random_seed" "$sample_rate")"
  IFS=$'\t' read -r ops controls peak realtime_factor <<< "$result"
  printf -v peak '%.4f' "$peak"
  printf -v realtime_factor '%.4f' "$realtime_factor"
  rows+=("$preset"$'\t'"$ops"$'\t'"$controls"$'\t'"$peak"$'\t'"$realtime_factor")
  ((${#preset} > name_width)) && name_width=${#preset}
  ((${#ops} > ops_width)) && ops_width=${#ops}
  ((${#controls} > controls_width)) && controls_width=${#controls}
  ((${#peak} > peak_width)) && peak_width=${#peak}
  ((${#realtime_factor} > rt_width)) && rt_width=${#realtime_factor}
done

printf "%-${name_width}s  %${ops_width}s  %${controls_width}s  %${peak_width}s  %${rt_width}s\n" \
  "$header_name" "$header_ops" "$header_controls" "$header_peak" "$header_rt"
for row in "${rows[@]}"; do
  IFS=$'\t' read -r preset ops controls peak realtime_factor <<< "$row"
  printf "%-${name_width}s  %${ops_width}s  %${controls_width}s  %${peak_width}s  %${rt_width}s\n" \
    "$preset" "$ops" "$controls" "$peak" "$realtime_factor"
done
