#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/audiovm-ops.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT

random_seed="${1:-$(date +%s)}"
printf 'Random seed: %s\n' "$random_seed"
cd "$project_dir"

"${CXX:-c++}" \
  -std=c++17 -O3 \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/tests/TestOperators.cpp" \
  -o "$temp_dir/test-ops"

results="$($temp_dir/test-ops "$random_seed")"

header_name=operator
header_inputs=inputs
header_outputs=outputs
header_rate='max rate'
header_rt='RT (ppm)'
name_width=${#header_name}
inputs_width=${#header_inputs}
outputs_width=${#header_outputs}
rate_width=${#header_rate}
rt_width=${#header_rt}
max_channels=1

while IFS=$'\t' read -r -a fields; do
  name=${fields[0]}
  inputs=${fields[1]}
  outputs=${fields[2]}
  rate=${fields[3]}
  ((${#name} > name_width)) && name_width=${#name}
  ((${#inputs} > inputs_width)) && inputs_width=${#inputs}
  ((${#outputs} > outputs_width)) && outputs_width=${#outputs}
  ((${#rate} > rate_width)) && rate_width=${#rate}
  channels=$((${#fields[@]} - 4))
  ((channels > max_channels)) && max_channels=$channels
  for ((channel = 0; channel < channels; ++channel)); do
    realtime_factor=${fields[$((channel + 4))]}
    ((${#realtime_factor} > rt_width)) && rt_width=${#realtime_factor}
  done
done <<< "$results"

printf "%-${name_width}s  %${inputs_width}s  %${outputs_width}s  %${rate_width}s  %${rt_width}s" \
  "$header_name" "$header_inputs" "$header_outputs" "$header_rate" "$header_rt"
for ((channel = 2; channel <= max_channels; ++channel)); do
  printf "  %${rt_width}s" "RT ${channel}ch"
done
printf '\n'
while IFS=$'\t' read -r -a fields; do
  printf "%-${name_width}s  %${inputs_width}s  %${outputs_width}s  %${rate_width}s" \
    "${fields[0]}" "${fields[1]}" "${fields[2]}" "${fields[3]}"
  for ((channel = 1; channel <= max_channels; ++channel)); do
    realtime_factor=${fields[$((channel + 3))]:--}
    printf "  %${rt_width}s" "$realtime_factor"
  done
  printf '\n'
done <<< "$results"
