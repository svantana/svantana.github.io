#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/audiovm-presets.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT

"${CXX:-c++}" \
  -std=c++17 -O2 \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/cpp/TestPresets.cpp" \
  -o "$temp_dir/test-presets"

found_preset=false
while IFS= read -r -d '' preset; do
  found_preset=true
  PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$project_dir/python" \
    python3 "$project_dir/tests/compile_preset.py" "$preset" \
    | "$temp_dir/test-presets" "$preset"
done < <(find "$project_dir/web/presets" -type f -name '*.py' -print0)

if ! "$found_preset"; then
  echo "No preset files found in $project_dir/web/presets" >&2
  exit 1
fi
