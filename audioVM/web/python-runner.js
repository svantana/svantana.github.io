const pyodideBase = 'https://cdn.jsdelivr.net/pyodide/v314.0.7/full/';
const audioVMUrl = new URL('../python/audioVM.py', import.meta.url);

export const starterPatch = String.raw`import audioVM

vm = audioVM.VM()

# Controls become the sliders on the right.
frequency = vm.control("frequency", 220)
level = vm.control("level", 0.12)

oscillator = vm.op("oscAnalog", {"note": frequency})
amplifier = oscillator * level
vm.op("audioOut", amplifier)
`;

export class PythonPatchRunner {
    #pyodide;
    #preparation;

    async prepare(onStatus = () => {}) {
        if (!this.#preparation) {
            this.#preparation = this.#initialize(onStatus).catch(cause => {
                this.#preparation = undefined;
                this.#pyodide = undefined;
                throw cause;
            });
        }
        return this.#preparation;
    }

    async compile(source, onStatus = () => {}) {
        await this.prepare(onStatus);
        this.#pyodide.globals.set('patch_source', source);
        return JSON.parse(await this.#pyodide.runPythonAsync(`
import json
import warnings
namespace = {}
with warnings.catch_warnings(record=True) as caught_warnings:
    warnings.simplefilter("always", RuntimeWarning)
    namespace["__file__"] = "/presets/patch.py"
    exec(compile(patch_source, namespace["__file__"], "exec"), namespace)
    vm = namespace.get("vm")
    if vm is None:
        raise RuntimeError("Your patch must assign its VM instance to a variable named vm.")
    graph_json = vm.get_json()
json.dumps({
    "graph": graph_json,
    "warnings": [{
        "message": str(warning.message),
        "category": warning.category.__name__,
        "filename": warning.filename,
        "lineno": warning.lineno,
    } for warning in caught_warnings],
})
`));
    }

    async #initialize(onStatus) {
        onStatus('Loading Python runtime…');
        await this.#loadRuntime();
        const pyodide = await window.loadPyodide({ indexURL: pyodideBase });
        const audioVMSource = await this.#loadAudioVMSource();
        const manifest = await fetch(new URL('./presets.json', import.meta.url));
        if (!manifest.ok) throw new Error(`Could not load preset list (${manifest.status}).`);
        const presets = await manifest.json();
        pyodide.FS.mkdirTree('/presets');
        const dependencies = ['midi/arpeggiator.py', 'midi/synth.py'];
        await Promise.all([...presets, ...dependencies].map(async name => {
            const response = await fetch(new URL(name, new URL('./presets/', import.meta.url)));
            if (!response.ok) throw new Error(`Could not load preset ${name} (${response.status}).`);
            const path = '/presets/' + name;
            pyodide.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
            pyodide.FS.writeFile(path, await response.text(), { encoding: 'utf8' });
        }));
        pyodide.globals.set('audiovm_source', audioVMSource);
        await pyodide.runPythonAsync(`
import sys, types
audioVM = types.ModuleType("audioVM")
audioVM.__file__ = "audioVM.py"
exec(compile(audiovm_source, audioVM.__file__, "exec"), audioVM.__dict__)
sys.modules["audioVM"] = audioVM
`);
        this.#pyodide = pyodide;
    }

    async #loadAudioVMSource() {
        const response = await fetch(audioVMUrl);
        if (!response.ok) {
            throw new Error(`Could not load python/audioVM.py (${response.status}).`);
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
