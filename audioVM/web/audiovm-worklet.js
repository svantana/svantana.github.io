import createAudioVM from './audiovm.js';

class AudioVMProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.wasmReady = null;
    this.processor = 0;
    this.inputPointer = 0;
    this.outputPointer = 0;
    this.heap = null;
    this.blockSize = 128;
    this.outputChannels = 2;
    this.smoothedCpuLoad = 0;
    this.cpuSmoothingSeconds = 0.2;
    this.lastStatsTime = 0;
    this.port.onmessage = event => this.handleMessage(event.data);
    this.port.postMessage({ type: 'status', message: 'Audio worklet ready' });
  }

  writeString(text) {
    const bytes = [];
    for (let index = 0; index < text.length; ++index) {
      let codePoint = text.codePointAt(index);
      if (codePoint > 0xffff) ++index;
      if (codePoint <= 0x7f) {
        bytes.push(codePoint);
      } else if (codePoint <= 0x7ff) {
        bytes.push(0xc0 | (codePoint >> 6), 0x80 | (codePoint & 0x3f));
      } else if (codePoint <= 0xffff) {
        bytes.push(0xe0 | (codePoint >> 12), 0x80 | ((codePoint >> 6) & 0x3f), 0x80 | (codePoint & 0x3f));
      } else {
        bytes.push(
          0xf0 | (codePoint >> 18),
          0x80 | ((codePoint >> 12) & 0x3f),
          0x80 | ((codePoint >> 6) & 0x3f),
          0x80 | (codePoint & 0x3f),
        );
      }
    }
    bytes.push(0);
    const pointer = this.wasm._malloc(bytes.length);
    this.wasm.HEAPU8.set(bytes, pointer);
    return pointer;
  }

  readString(pointer) {
    let end = pointer;
    while (this.wasm.HEAPU8[end]) ++end;
    return new TextDecoder().decode(this.wasm.HEAPU8.subarray(pointer, end));
  }

  async handleMessage(message) {
    try {
      if (message.type === 'load') {
        this.port.postMessage({ type: 'status', message: 'Loading AudioVM WASM…' });
        if (!this.wasmReady) this.wasmReady = createAudioVM({
          wasmBinary: message.wasmBinary,
          locateFile: path => path,
        });
        this.wasm = await this.wasmReady;
        this.port.postMessage({ type: 'status', message: 'Configuring AudioVM…' });
        if (this.processor) this.wasm._audiovm_destroy(this.processor);
        this.processor = this.wasm._audiovm_create();
        this.wasm._audiovm_setup(this.processor, sampleRate, 1, this.outputChannels, this.blockSize);
        const jsonPointer = this.writeString(message.json);
        const loaded = this.wasm._audiovm_load_from_json(this.processor, jsonPointer);
        this.wasm._free(jsonPointer);
        if (!loaded) {
          const message = this.readString(this.wasm._audiovm_get_last_error());
          throw new Error(message || 'AudioVM rejected the patch.');
        }
        this.inputPointer = this.wasm._malloc(this.blockSize * 4);
        this.outputPointer = this.wasm._malloc(this.blockSize * this.outputChannels * 4);
        this.heap = new Float32Array(this.wasm.HEAPU8.buffer);
        this.port.postMessage({ type: 'loaded', requestId: message.requestId, sampleRate, blockSize: this.blockSize });
      } else if (message.type === 'control' && this.processor) {
        this.wasm._audiovm_set_control_value(this.processor, message.index, message.value);
      } else if (message.type === 'midi' && this.processor) {
        this.wasm._audiovm_midi_event(this.processor, message.status, message.data1, message.data2);
      } else if (message.type === 'transport' && this.processor) {
        if (Number.isFinite(message.bpm)) this.wasm._audiovm_set_bpm(this.processor, message.bpm);
        if (typeof message.play === 'boolean') this.wasm._audiovm_set_play(this.processor, message.play ? 1 : 0);
        if (Number.isFinite(message.beatPos)) this.wasm._audiovm_set_beat_pos(this.processor, message.beatPos);
        if (Number.isFinite(message.loopBeats)) this.wasm._audiovm_set_loop(this.processor, message.loopBeats);
      }
    } catch (cause) {
      this.port.postMessage({ type: 'error', requestId: message.requestId, message: cause.message || String(cause) });
    }
  }

  process(inputs, outputs) {
    const output = outputs[0];
    if (!this.processor || !this.wasm) {
      output.forEach(channel => channel.fill(0));
      return true;
    }

    const frames = output[0].length;
    const input = inputs[0] || [];
    const start = Date.now();
    let heap = this.heap;
    // A module compiled with memory growth can replace its backing buffer.
    if (heap.buffer !== this.wasm.HEAPU8.buffer) {
      heap = this.heap = new Float32Array(this.wasm.HEAPU8.buffer);
    }
    heap.fill(0, this.inputPointer >> 2, (this.inputPointer >> 2) + frames);
    if (input[0]) heap.set(input[0], this.inputPointer >> 2);
    this.wasm._audiovm_process(this.processor, this.inputPointer, this.outputPointer, frames, 1, this.outputChannels);
    for (let channel = 0; channel < output.length; ++channel) {
      const offset = (this.outputPointer >> 2) + channel * frames;
      output[channel].set(heap.subarray(offset, offset + frames));
    }

    const elapsed = Date.now() - start;
    const callbackLoad = 100 * elapsed / (frames * 1000 / sampleRate);
    const smoothing = 1 - Math.exp(-frames / (sampleRate * this.cpuSmoothingSeconds));
    this.smoothedCpuLoad += smoothing * (callbackLoad - this.smoothedCpuLoad);
    if (currentTime - this.lastStatsTime >= 0.25) {
        this.port.postMessage({
          type: 'stats', cpu: Math.max(0, this.smoothedCpuLoad), sampleRate, blockSize: frames,
          beatPos: this.wasm._audiovm_get_beat_pos(this.processor),
        });
      this.lastStatsTime = currentTime;
    }
    return true;
  }
}

registerProcessor('audiovm-processor', AudioVMProcessor);
