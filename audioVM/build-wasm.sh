#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
output_dir="$project_dir/web"
mkdir -p "$output_dir"

# Homebrew's Emscripten installation may live in a read-only Cellar path.
# Keep its generated cache outside the checkout unless the caller supplied one.
cache_dir="${EM_CACHE:-${TMPDIR:-/tmp}/audiovm-emscripten-cache}"
mkdir -p "$cache_dir"
# On macOS, /var is a symlink to /private/var.  Emscripten passes system
# sources as paths relative to EM_CACHE, so use the physical path to ensure
# Clang resolves those paths from the same location.
export EM_CACHE="$(cd "$cache_dir" && pwd -P)"

# Keep the browser's Python builder in sync with the canonical source file.
cp "$project_dir/python/audioVM.py" "$output_dir/audioVM.py"

# A browser cannot list a static directory, so expose the available Python
# preset files through a manifest beside the presets folder.
python3 -c 'import json, pathlib, sys; root = pathlib.Path(sys.argv[1]); files = sorted(str(path.relative_to(root)) for path in root.rglob("*.py")); pathlib.Path(sys.argv[2]).write_text(json.dumps(files))' \
  "$output_dir/presets" "$output_dir/presets.json"

em++ \
  "$project_dir/cpp/AudioVM.cpp" \
  "$project_dir/cpp/AudioVMEmscripten.cpp" \
  --std=c++17 \
  -O3 \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s EXPORT_NAME=createAudioVM \
  -s ENVIRONMENT=web,worker \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s FILESYSTEM=0 \
  -s NO_EXIT_RUNTIME=1 \
  -s INCOMING_MODULE_JS_API='["wasmBinary"]' \
  -s EXPORTED_FUNCTIONS='["_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
  --no-entry \
  -o "$output_dir/audiovm.js"
