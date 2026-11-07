#include "AudioVM.h"

#include <emscripten/emscripten.h>

namespace {

// Returned JSON remains valid until the next audiovm_get_*_json call.
std::string jsonResult;

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE AudioVM::Processor* audiovm_create() {
    return new AudioVM::Processor();
}

EMSCRIPTEN_KEEPALIVE void audiovm_destroy(AudioVM::Processor* processor) {
    delete processor;
}

EMSCRIPTEN_KEEPALIVE void audiovm_setup(AudioVM::Processor* processor,
                                        float sampleRate,
                                        int inputChannels,
                                        int outputChannels,
                                        int blockSize) {
    processor->setup(sampleRate, inputChannels, outputChannels, blockSize);
}

EMSCRIPTEN_KEEPALIVE void audiovm_load_from_json(AudioVM::Processor* processor,
                                                 const char* json) {
    processor->loadFromJSON(json);
}

EMSCRIPTEN_KEEPALIVE const char* audiovm_get_op_list_json(AudioVM::Processor* processor) {
    jsonResult = processor->getOpListAsJSON();
    return jsonResult.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* audiovm_get_op_definitions_json(AudioVM::Processor* processor) {
    jsonResult = processor->getOpDefinitionsJSON();
    return jsonResult.c_str();
}

EMSCRIPTEN_KEEPALIVE int audiovm_get_num_controls(AudioVM::Processor* processor) {
    return processor->getNumControls();
}

EMSCRIPTEN_KEEPALIVE float audiovm_get_control_value(AudioVM::Processor* processor, int index) {
    return processor->getControlValue(index);
}

EMSCRIPTEN_KEEPALIVE void audiovm_set_control_value(AudioVM::Processor* processor,
                                                     int index,
                                                     float value) {
    processor->setControlValue(index, value);
}

EMSCRIPTEN_KEEPALIVE void audiovm_midi_event(AudioVM::Processor* processor,
                                              int status,
                                              int data1,
                                              int data2) {
    processor->midiEvent(static_cast<char>(status), static_cast<char>(data1), static_cast<char>(data2));
}

// Input and output point to planar channel buffers in Wasm memory. Their
// channel counts must match the values passed to audiovm_setup.
EMSCRIPTEN_KEEPALIVE void audiovm_process(AudioVM::Processor* processor,
                                          float* input,
                                          float* output,
                                          int numSamples,
                                          int inputChannels,
                                          int outputChannels) {
    // Channel counts are fixed by audiovm_setup.  They remain parameters for
    // backwards-compatible JS calls, but pointer arrays now live in Processor
    // rather than being allocated for every 128-frame callback.
    (void)inputChannels;
    (void)outputChannels;
    processor->processPlanar(input, output, numSamples);
}

} // extern "C"
