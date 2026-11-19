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

while IFS=$'\t' read -r name inputs outputs rate realtime_factor; do
  ((${#name} > name_width)) && name_width=${#name}
  ((${#inputs} > inputs_width)) && inputs_width=${#inputs}
  ((${#outputs} > outputs_width)) && outputs_width=${#outputs}
  ((${#rate} > rate_width)) && rate_width=${#rate}
  ((${#realtime_factor} > rt_width)) && rt_width=${#realtime_factor}
done <<< "$results"

printf "%-${name_width}s  %${inputs_width}s  %${outputs_width}s  %${rate_width}s  %${rt_width}s\n" \
  "$header_name" "$header_inputs" "$header_outputs" "$header_rate" "$header_rt"
while IFS=$'\t' read -r name inputs outputs rate realtime_factor; do
  printf "%-${name_width}s  %${inputs_width}s  %${outputs_width}s  %${rate_width}s  %${rt_width}s\n" \
    "$name" "$inputs" "$outputs" "$rate" "$realtime_factor"
done <<< "$results"
