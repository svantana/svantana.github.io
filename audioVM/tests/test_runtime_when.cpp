#include "../cpp/AudioVM.h"

#include <algorithm>
#include <cmath>
#include <iostream>

int main() {
    AudioVM::Processor invalidOperatorProcessor;
    invalidOperatorProcessor.setup(44100.f, 1, 1, 16);
    try {
        invalidOperatorProcessor.loadFromJSON(R"({"version":1,"program":[
            {"type":"oscMystery","id":"unknown"}
        ]})");
        std::cerr << "unknown operator type was accepted\n";
        return 1;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find("program[0]") == std::string::npos ||
            message.find("oscMystery") == std::string::npos) {
            std::cerr << "unknown operator error was not informative: " << message << "\n";
            return 1;
        }
    }

    // An envelope must not create a pulse until its gate has a rising edge.
    AudioVM::Processor envelopeProcessor;
    envelopeProcessor.setup(44100.f, 1, 1, 16);
    envelopeProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"control","id":"gate","input":0},
        {"type":"envExp","id":"envelope","input":{"gate":"gate","decay":0.1}},
        {"type":"toAudio","id":"signal","input":"envelope"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    float envelopeOutput[16]{};
    float* envelopeOutputs[] = {envelopeOutput};
    envelopeProcessor.process(nullptr, envelopeOutputs, 16);
    for (float sample : envelopeOutput) {
        if (sample != 0.f) {
            std::cerr << "idle exponential envelope produced a pulse\n";
            return 1;
        }
    }
    envelopeProcessor.setControlValue(0, 1.f);
    envelopeProcessor.process(nullptr, envelopeOutputs, 16);
    if (*std::max_element(envelopeOutput, envelopeOutput + 16) <= 0.f) {
        std::cerr << "triggered exponential envelope produced silence\n";
        return 1;
    }

    // Delay capacity comes from its initialized input buffer. It need not
    // inspect the source operator or use a compiler-only configuration path.
    AudioVM::Processor delayProcessor;
    delayProcessor.setup(44100.f, 1, 1, 16);
    delayProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"value","id":"capacity","input":0.1},
        {"type":"audioIn","id":"input"},
        {"type":"delay","id":"delay","input":{"input":"input","time":0.01,"maxSeconds":"capacity"}},
        {"type":"audioOut","id":"out","input":"delay"}
    ]})");
    float delayInput[16]{};
    float delayOutput[16]{};
    float* delayInputs[] = {delayInput};
    float* delayOutputs[] = {delayOutput};
    delayProcessor.process(delayInputs, delayOutputs, 16);

    AudioVM::Processor processor;
    processor.setup(44100.f, 1, 1, 16);
    processor.loadFromJSON(R"({"version":1,"polyphony":1,"program":[
        {"type":"control","id":"enabled","input":0},
        {"type":"control","id":"release","input":0.001},
        {"type":"ifStart","id":"ifStart1","input":{"condition":"enabled","timeOut":"release"}},
        {"type":"oscNoise","id":"signal"},
        {"type":"ifEnd","id":"ifEnd1"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");

    float output[16]{};
    float* outputs[] = {output};
    processor.process(nullptr, outputs, 16);
    for (float sample : output) {
        if (sample != 0.f) {
            std::cerr << "disabled conditional range produced " << sample << "\n";
            return 1;
        }
    }

    processor.setControlValue(0, 1.f);
    processor.process(nullptr, outputs, 16);
    bool producedAudio = false;
    for (float sample : output) {
        producedAudio = producedAudio || std::abs(sample) > 1e-6f;
    }
    if (!producedAudio) {
        std::cerr << "enabled conditional range did not run\n";
        return 1;
    }

    processor.setControlValue(0, 0.f);
    processor.process(nullptr, outputs, 16);
    producedAudio = false;
    for (float sample : output) producedAudio = producedAudio || std::abs(sample) > 1e-6f;
    if (!producedAudio) {
        std::cerr << "conditional timeout did not keep the range running\n";
        return 1;
    }
    for (int block = 0; block < 3; ++block) processor.process(nullptr, outputs, 16);
    for (float sample : output) {
        if (sample != 0.f) {
            std::cerr << "conditional timeout did not end\n";
            return 1;
        }
    }

    // blocksize is a program-level setting, so it changes how an incoming
    // host buffer is split into processing blocks.
    AudioVM::Processor blockSizeProcessor;
    blockSizeProcessor.setup(44100.f, 1, 1, 16);
    blockSizeProcessor.loadFromJSON(R"({"version":1,"blocksize":8,"program":[
        {"type":"control","id":"enabled","input":0},
        {"type":"control","id":"release","input":0.0003},
        {"type":"ifStart","id":"ifStart1","input":{"condition":"enabled","timeOut":"release"}},
        {"type":"oscNoise","id":"signal"},
        {"type":"ifEnd","id":"ifEnd1"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    float blockSizeOutput[16]{};
    float* blockSizeOutputs[] = {blockSizeOutput};
    blockSizeProcessor.setControlValue(0, 1.f);
    blockSizeProcessor.process(nullptr, blockSizeOutputs, 8);
    blockSizeProcessor.setControlValue(0, 0.f);
    blockSizeProcessor.process(nullptr, blockSizeOutputs, 16);
    bool processedFirstBlock = false;
    for (int sample = 0; sample < 8; ++sample) processedFirstBlock = processedFirstBlock || blockSizeOutput[sample] != 0.f;
    if (!processedFirstBlock) {
        std::cerr << "JSON blocksize did not preserve the first release block\n";
        return 1;
    }
    for (int sample = 8; sample < 16; ++sample) {
        if (blockSizeOutput[sample] != 0.f) {
            std::cerr << "JSON blocksize did not split the host buffer\n";
            return 1;
        }
    }

    AudioVM::Processor transportProcessor;
    transportProcessor.setup(16.f, 1, 1, 16);
    transportProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"toAudio","id":"position","input":"transport.beatPos"},
        {"type":"audioOut","id":"out","input":"position"}
    ]})");
    transportProcessor.setBPM(60.f);
    transportProcessor.setLoop(2.f);
    transportProcessor.setPlay(true);
    float transportOutput[16]{};
    float* transportOutputs[] = {transportOutput};
    transportProcessor.process(nullptr, transportOutputs, 16);
    if (std::abs(transportOutput[15]) > 1e-6f || std::abs(transportProcessor.getBeatPos() - 1.f) > 1e-6f) {
        std::cerr << "transport did not expose the initial beat or advance one beat\n";
        return 1;
    }
    transportProcessor.process(nullptr, transportOutputs, 16);
    if (std::abs(transportOutput[15] - 1.f) > 1e-6f || std::abs(transportProcessor.getBeatPos()) > 1e-6f) {
        std::cerr << "transport loop did not wrap at its requested length\n";
        return 1;
    }

    AudioVM::Processor midiBufferProcessor;
    midiBufferProcessor.setup(44100.f, 1, 1, 16);
    midiBufferProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"midiBuffer","id":"arp"},
        {"type":"toAudio","id":"note","input":"arp.note"},
        {"type":"audioOut","id":"out","input":"note"}
    ]})");
    if (!midiBufferProcessor.setMidiOutput("arp") ||
        !midiBufferProcessor.midiEvent("arp", static_cast<char>(0x90), 64, 100)) {
        std::cerr << "named MIDI buffer could not be configured\n";
        return 1;
    }
    float midiBufferOutput[16]{};
    float* midiBufferOutputs[] = {midiBufferOutput};
    midiBufferProcessor.process(nullptr, midiBufferOutputs, 16);
    AudioVM::MidiEvent midiOutputEvent{};
    if (std::abs(midiBufferOutput[15] - 64.f) > 1e-6f ||
        !midiBufferProcessor.readMidiOutput(midiOutputEvent) ||
        midiOutputEvent.status != 0x90 || midiOutputEvent.data1 != 64 || midiOutputEvent.data2 != 100) {
        std::cerr << "named MIDI buffer did not expose its processed event\n";
        return 1;
    }

    // A MIDI buffer used directly by a synth must sound the same as an event
    // forwarded through one processor's MIDI output into another processor.
    const char* directSynth = R"({"version":1,"program":[
        {"type":"midiBuffer","id":"generator"},
        {"type":"oscSaw","id":"osc","input":{"note":"generator.note"}},
        {"type":"envADSR","id":"env","input":{"gate":"generator.gate","a":0.001,"d":0.1,"s":0.5,"r":0.1}},
        {"type":"mul","id":"amp","input":["osc","env"]},
        {"type":"audioOut","id":"out","input":"amp"}
    ]})";
    const char* receivingSynth = R"({"version":1,"program":[
        {"type":"oscSaw","id":"osc","input":{"note":"midi.note"}},
        {"type":"envADSR","id":"env","input":{"gate":"midi.gate","a":0.001,"d":0.1,"s":0.5,"r":0.1}},
        {"type":"mul","id":"amp","input":["osc","env"]},
        {"type":"audioOut","id":"out","input":"amp"}
    ]})";
    AudioVM::Processor directProcessor;
    directProcessor.setup(44100.f, 1, 1, 16);
    directProcessor.loadFromJSON(directSynth);
    if (!directProcessor.midiEvent("generator", static_cast<char>(0x90), 60, 100)) {
        std::cerr << "direct synth could not receive generator MIDI\n";
        return 1;
    }
    float directOutput[16]{};
    float* directOutputs[] = {directOutput};
    directProcessor.process(nullptr, directOutputs, 16);

    AudioVM::Processor generatorProcessor;
    generatorProcessor.setup(44100.f, 1, 1, 16);
    generatorProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"midiBuffer","id":"generator"}
    ]})");
    if (!generatorProcessor.setMidiOutput("generator") ||
        !generatorProcessor.midiEvent("generator", static_cast<char>(0x90), 60, 100)) {
        std::cerr << "generator processor could not produce MIDI\n";
        return 1;
    }
    generatorProcessor.process(nullptr, nullptr, 16);
    AudioVM::MidiEvent generatedEvent{};
    if (!generatorProcessor.readMidiOutput(generatedEvent)) {
        std::cerr << "generator processor produced no MIDI output\n";
        return 1;
    }

    AudioVM::Processor receivingProcessor;
    receivingProcessor.setup(44100.f, 1, 1, 16);
    receivingProcessor.loadFromJSON(receivingSynth);
    receivingProcessor.midiEvent(static_cast<char>(generatedEvent.status),
                                  static_cast<char>(generatedEvent.data1),
                                  static_cast<char>(generatedEvent.data2));
    float receivedOutput[16]{};
    float* receivedOutputs[] = {receivedOutput};
    receivingProcessor.process(nullptr, receivedOutputs, 16);
    bool directIsAudible = false;
    for (int sample = 0; sample < 16; ++sample) {
        directIsAudible = directIsAudible || std::abs(directOutput[sample]) > 1e-6f;
        if (std::abs(directOutput[sample] - receivedOutput[sample]) > 1e-6f) {
            std::cerr << "forwarded MIDI did not reproduce direct synth output\n";
            return 1;
        }
    }
    if (!directIsAudible) {
        std::cerr << "direct MIDI synth produced silence\n";
        return 1;
    }

    // A direct oscillator must reach audioOut unchanged. This also guards
    // against accidental gain being introduced by channel/output routing.
    AudioVM::Processor sineProcessor;
    sineProcessor.setup(44100.f, 1, 1, 128);
    sineProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"oscSin","id":"osc","input":{"frequency":110}},
        {"type":"audioOut","id":"out","input":"osc"}
    ]})");
    float sineOutput[44100]{};
    float* sineOutputs[] = {sineOutput};
    sineProcessor.process(nullptr, sineOutputs, 44100);
    float sinePeak = 0.f;
    for (float sample : sineOutput) sinePeak = std::max(sinePeak, std::abs(sample));
    if (sinePeak < 0.99f || sinePeak > 1.01f) {
        std::cerr << "oscSin direct output peak was " << sinePeak << ", expected approximately 1\n";
        return 1;
    }

    AudioVM::Processor scaledSineProcessor;
    scaledSineProcessor.setup(44100.f, 1, 1, 128);
    scaledSineProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"oscSin","id":"osc","input":{"frequency":110}},
        {"type":"const","id":"gain","input":0.5},
        {"type":"mul","id":"scaled","input":["osc","gain"]},
        {"type":"audioOut","id":"out","input":"scaled"}
    ]})");
    float scaledSineOutput[44100]{};
    float* scaledSineOutputs[] = {scaledSineOutput};
    scaledSineProcessor.process(nullptr, scaledSineOutputs, 44100);
    float scaledSinePeak = 0.f;
    for (float sample : scaledSineOutput) scaledSinePeak = std::max(scaledSinePeak, std::abs(sample));
    if (scaledSinePeak < 0.495f || scaledSinePeak > 0.505f) {
        std::cerr << "scaled oscSin output peak was " << scaledSinePeak << ", expected approximately 0.5\n";
        return 1;
    }

    // Tables retain all their values in VM memory. Interp must handle both
    // in-range fractional positions and endpoint-slope extrapolation.
    AudioVM::Processor interpProcessor;
    interpProcessor.setup(44100.f, 1, 1, 5);
    interpProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"array","id":"table","input":[0,10,30]},
        {"type":"audioIn","id":"index"},
        {"type":"interp","id":"lookup","input":["table","index"]},
        {"type":"audioOut","id":"out","input":"lookup"}
    ]})");
    float lookupIndices[] = {-1.f, 0.5f, 1.f, 2.f, 3.f};
    float lookupOutput[5]{};
    float* lookupInputs[] = {lookupIndices};
    float* lookupOutputs[] = {lookupOutput};
    interpProcessor.process(lookupInputs, lookupOutputs, 5);
    const float expectedLookup[] = {-10.f, 5.f, 10.f, 30.f, 50.f};
    for (int sample = 0; sample < 5; ++sample) {
        if (std::abs(lookupOutput[sample] - expectedLookup[sample]) > 1e-6f) {
            std::cerr << "interp produced " << lookupOutput[sample] << " at sample " << sample
                      << ", expected " << expectedLookup[sample] << "\n";
            return 1;
        }
    }

    // A normal writable buffer can be used as the lookup table as well.
    AudioVM::Processor bufferInterpProcessor;
    bufferInterpProcessor.setup(44100.f, 1, 1, 5);
    bufferInterpProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"audioIn","id":"source"},
        {"type":"buffer","id":"table"},
        {"type":"iadd","id":"fill","input":["table","source"]},
        {"type":"interp","id":"lookup","input":["table","source"]},
        {"type":"audioOut","id":"out","input":"lookup"}
    ]})");
    float bufferIndices[] = {0.f, 1.f, 2.f, 3.f, 4.f};
    float bufferLookupOutput[5]{};
    float* bufferLookupInputs[] = {bufferIndices};
    float* bufferLookupOutputs[] = {bufferLookupOutput};
    bufferInterpProcessor.process(bufferLookupInputs, bufferLookupOutputs, 5);
    for (int sample = 0; sample < 5; ++sample) {
        if (std::abs(bufferLookupOutput[sample] - static_cast<float>(sample)) > 1e-6f) {
            std::cerr << "interp did not accept a standard buffer at sample " << sample << "\n";
            return 1;
        }
    }

    // Values are persistent scalar registers. Reads snapshot their current
    // contents, and several writes in one block are visible in program order.
    AudioVM::Processor valueProcessor;
    valueProcessor.setup(44100.f, 1, 1, 16);
    valueProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"value","id":"counter","input":2},
        {"type":"const","id":"one","input":1},
        {"type":"value","id":"first","input":"counter"},
        {"type":"add","id":"next1","input":["first","one"]},
        {"type":"valueWrite","id":"write1","input":["counter","next1"]},
        {"type":"value","id":"second","input":"counter"},
        {"type":"add","id":"next2","input":["second","one"]},
        {"type":"valueWrite","id":"write2","input":["counter","next2"]},
        {"type":"value","id":"result","input":"counter"},
        {"type":"toAudio","id":"signal","input":"result"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    float valueOutput[16]{};
    float* valueOutputs[] = {valueOutput};
    valueProcessor.process(nullptr, valueOutputs, 16);
    if (std::abs(valueOutput[15] - 4.f) > 1e-6f) {
        std::cerr << "value did not apply multiple ordered writes\n";
        return 1;
    }
    valueProcessor.process(nullptr, valueOutputs, 16);
    if (std::abs(valueOutput[15] - 6.f) > 1e-6f) {
        std::cerr << "value did not persist between processing blocks\n";
        return 1;
    }

    // A polyphonic range can reduce multiple simultaneous MIDI events into a
    // block-reset mutable array, which ordinary monophonic graph sections can
    // read after the range.  The conditional write is important: old active
    // voices must not overwrite a newly set slot with a zero onset.
    AudioVM::Processor arrayProcessor;
    arrayProcessor.setup(44100.f, 1, 1, 16);
    arrayProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"arrayState","id":"onsets","input":[0,0,0,0,0,0,0,0,0,0,0,0]},
        {"type":"polyStart","id":"voices","input":{"count":12}},
        {"type":"const","id":"twelve","input":12},
        {"type":"mod","id":"noteClass","input":["midi.note","twelve"]},
        {"type":"ifStart","id":"hasOnset","input":"midi.onset"},
        {"type":"arrayWrite","id":"writeOnset","input":["onsets","noteClass","midi.onset"]},
        {"type":"ifEnd","id":"endOnset"},
        {"type":"polyEnd","id":"endVoices"},
        {"type":"arrayRead","id":"kick","input":["onsets",0]},
        {"type":"arrayRead","id":"snare","input":["onsets",2]},
        {"type":"add","id":"mixedOnsets","input":["kick","snare"]},
        {"type":"toAudio","id":"signal","input":"mixedOnsets"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    arrayProcessor.midiEvent(static_cast<char>(0x90), 60, 100);
    arrayProcessor.midiEvent(static_cast<char>(0x90), 62, 100);
    float arrayOutput[16]{};
    float* arrayOutputs[] = {arrayOutput};
    arrayProcessor.process(nullptr, arrayOutputs, 16);
    if (std::abs(arrayOutput[15] - 2.f) > 1e-6f) {
        std::cerr << "polyphonic array event fan-in lost simultaneous onsets\n";
        return 1;
    }
    arrayProcessor.midiEvent(static_cast<char>(0x90), 60, 100);
    arrayProcessor.process(nullptr, arrayOutputs, 16);
    if (std::abs(arrayOutput[15] - 1.f) > 1e-6f) {
        std::cerr << "an old polyphonic voice cleared a new array onset\n";
        return 1;
    }
    // No events: the array must return to its initial contents, rather than
    // retaining the onset written by the prior block.
    arrayProcessor.process(nullptr, arrayOutputs, 16);
    if (std::abs(arrayOutput[15]) > 1e-6f) {
        std::cerr << "mutable array did not reset before the next block\n";
        return 1;
    }
    return 0;
}
