const pyodideBase = 'https://cdn.jsdelivr.net/pyodide/v314.0.7/full/';
const audioVMUrl = new URL('./audioVM.py', import.meta.url);

export const starterPatch = String.raw`import audioVM

vm = audioVM.VM()

# Controls become the sliders on the right.
frequency = vm.control("frequency", 220)
level = vm.control("level", 0.12)

oscillator = vm.op("oscSaw", {"note": frequency})
amplifier = oscillator * level
vm.op("audioOut", amplifier)
`;

export class PythonPatchRunner {
    #pyodide;

    async compile(source, onStatus = () => {}) {
        if (!this.#pyodide) {
            onStatus('Loading Python runtime…');
            await this.#loadRuntime();
            this.#pyodide = await window.loadPyodide({ indexURL: pyodideBase });
            const audioVMSource = await this.#loadAudioVMSource();
            this.#pyodide.globals.set('audiovm_source', audioVMSource);
            await this.#pyodide.runPythonAsync(`
import sys, types
audioVM = types.ModuleType("audioVM")
exec(audiovm_source, audioVM.__dict__)
sys.modules["audioVM"] = audioVM
`);
        }
        this.#pyodide.globals.set('patch_source', source);
        return await this.#pyodide.runPythonAsync(`
namespace = {}
exec(patch_source, namespace)
vm = namespace.get("vm")
if vm is None:
    raise RuntimeError("Your patch must assign its VM instance to a variable named vm.")
vm.get_json()
`);
    }

    async #loadAudioVMSource() {
        const response = await fetch(audioVMUrl);
        if (!response.ok) {
            throw new Error(`Could not load audioVM.py (${response.status}). Run build-wasm.sh first.`);
        }
        return await response.text();
    }

    #loadRuntime() {
        return new Promise((resolve, reject) => {
            if (window.loadPyodide) return resolve();
            const script = document.createElement('script');
            script.src = pyodideBase + 'pyodide.js';
            script.onload = resolve;
            script.onerror = () => reject(new Error('Could not download the Python runtime.'));
            document.head.append(script);
        });
    }
}
