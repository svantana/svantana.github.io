# AudioVM

AudioVM is a small DSP graph runtime. Write patches in Python; run them natively or in the browser via WebAssembly.

```python
import audioVM

vm = audioVM.VM()
osc = vm.op("oscSaw", {"note": 60})
vm.op("audioOut", osc * 0.1)
```

The browser player lives in [`web/`](web/). Presets live in [`web/presets/`](web/presets/).

## Development

```sh
./scripts/test-presets.sh
./scripts/build-wasm.sh
cd web && python3 -m http.server
```

`build-wasm.sh` requires Emscripten (`em++`).
