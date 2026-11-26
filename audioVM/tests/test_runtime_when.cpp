#include "../cpp/AudioVM.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

class CapacityProbe : public AudioVM::FullRateOp {
public:
    int maxNumChannels() const override { return 2; }
    int getNumPorts(AudioVM::Port::Dir dir) override { return dir == AudioVM::Port::Out ? 2 : 0; }
    void process(AudioVM::Buffer*, AudioVM::Buffer* out, int n) override {
        std::fill_n(out[0].data(1), n, 9.f);
        std::fill_n(out[1].data(), n, 3.f);
        std::copy_n(out[0].data(1), n, out[0].data());
    }
};

class ChannelSources : public AudioVM::FullRateOp {
public:
    int getNumPorts(AudioVM::Port::Dir dir) override { return dir == AudioVM::Port::Out ? 2 : 0; }
    void process(AudioVM::Buffer*, AudioVM::Buffer* out, int n) override {
        std::fill_n(out[0].data(), n, 0.25f);
        std::fill_n(out[1].data(), n, 0.75f);
    }
};

int main() {
    AudioVM::Processor killProcessor;
    killProcessor.setup(1000.f, 1, 1, 10);
    killProcessor.loadFromJSON(R"({"version":1,"blocksize":10,"program":[
        {"type":"audioIn","id":"in"},
        {"type":"buffer","id":"mix"},
        {"type":"polyStart","id":"poly","input":{"count":1}},
        {"type":"meanSquare","id":"ms","input":"in"},
        {"type":"less","id":"quiet","input":["ms",0.01]},
        {"type":"equal","id":"released","input":["midi.gate",0]},
        {"type":"mul","id":"killCondition","input":["quiet","released"]},
        {"type":"voiceKill","id":"kill","input":{"condition":"killCondition","timeOut":0.025}},
        {"type":"toAudio","id":"signal","input":1},
        {"type":"iadd","id":"add","input":["mix","signal"]},
        {"type":"polyEnd","id":"end"},
        {"type":"audioOut","id":"out","input":"mix"}
    ]})");
    if (killProcessor.getVoiceCapacity() != 1 || killProcessor.getActiveVoiceCount() != 0) return 1;
    float killIn[10]{}, killOut[10]{};
    float* killInputs[] = {killIn}; float* killOutputs[] = {killOut};
    auto runKill = [&](int n) { killProcessor.process(killInputs, killOutputs, n); return killOut[n-1]; };
    killProcessor.midiEvent(static_cast<char>(0x90), 60, 100);
    for (int i = 0; i < 5; ++i) if (runKill(10) < 0.9f) { std::cerr << "held quiet voice was killed\n"; return 1; }
    if (killProcessor.getActiveVoiceCount() != 1) return 1;
    killProcessor.midiEvent(static_cast<char>(0x80), 60, 0);
    if (runKill(10) < 0.9f) return 1;
    std::fill_n(killIn, 10, 0.2f);
    if (runKill(10) < 0.9f) return 1;
    std::fill_n(killIn, 10, 0.f);
    if (runKill(10) < 0.9f || runKill(10) < 0.9f || runKill(5) < 0.9f || runKill(10) != 0.f) {
        std::cerr << "voice kill timer or final block failed\n"; return 1;
    }
    if (killProcessor.getActiveVoiceCount() != 0) return 1;
    killProcessor.midiEvent(static_cast<char>(0x90), 61, 100);
    if (runKill(10) < 0.9f) { std::cerr << "voice kill did not reset on reuse\n"; return 1; }
    if (killProcessor.getActiveVoiceCount() != 1) return 1;

    AudioVM::Processor capacityProcessor;
    capacityProcessor.setup(44100.f, 0, 1, 16);
    capacityProcessor.registerType("capacityProbe", [] { return std::make_unique<CapacityProbe>(); }, "test");
    const int probe = capacityProcessor.addOperator("capacityProbe");
    capacityProcessor.addOperator("audioOut", {probe});
    capacityProcessor.compileProgram();
    float capacityOutput[16]{};
    float* capacityOutputs[] = {capacityOutput};
    capacityProcessor.process(nullptr, capacityOutputs, 16);
    if (!std::all_of(std::begin(capacityOutput), std::end(capacityOutput), [](float v) { return v == 9.f; })) {
        std::cerr << "operator output channel capacity overlaps the next buffer\n";
        return 1;
    }
    AudioVM::Processor routing;
    routing.setup(44100.f, 0, 2, 16);
    routing.registerType("channelSources", [] { return std::make_unique<ChannelSources>(); }, "test");
    const int sources = routing.addOperator("channelSources");
    const int joined = routing.addOperator("channels", {sources, sources + 1});
    const int index = routing.addConstant(1.f);
    const int right = routing.addOperator("channel", {joined, index});
    const int swapped = routing.addOperator("channels", {right, sources});
    routing.addOperator("audioOut", {swapped});
    routing.compileProgram();
    float leftOut[16]{}, rightOut[16]{};
    float* routeOutputs[] = {leftOut, rightOut};
    routing.process(nullptr, routeOutputs, 16);
    if (leftOut[0] != 0.75f || rightOut[0] != 0.25f) {
        std::cerr << "channel split/join did not preserve channel order\n";
        return 1;
    }
    AudioVM::Processor invalidChannel;
    invalidChannel.setup(44100.f, 0, 1, 16);
    const int mono = invalidChannel.addOperator("audioIn");
    const int invalidIndex = invalidChannel.addConstant(1.f);
    invalidChannel.addOperator("channel", {mono, invalidIndex});
    try {
        invalidChannel.compileProgram();
        std::cerr << "out-of-range channel index was accepted\n";
        return 1;
    } catch (const std::runtime_error&) {}
    AudioVM::Processor invalidJoin;
    invalidJoin.setup(44100.f, 1, 1, 16);
    const int joinInput = invalidJoin.addOperator("audioIn");
    const int joinPair = invalidJoin.addOperator("channels", {joinInput, joinInput});
    invalidJoin.addOperator("channels", {joinPair});
    try {
        invalidJoin.compileProgram();
        std::cerr << "multichannel input to channels was accepted\n";
        return 1;
    } catch (const std::runtime_error&) {}
    AudioVM::Processor jsonChannels;
    jsonChannels.setup(44100.f, 1, 1, 16);
    jsonChannels.loadFromJSON(R"({"version":1,"program":[
        {"type":"audioIn","id":"in"},
        {"type":"channels","id":"pair","input":["in","in"]},
        {"type":"channel","id":"right","input":{"input":"pair","index":1}},
        {"type":"audioOut","id":"out","input":"right"}
    ]})");
    float jsonInput[19], jsonOutput[19]{};
    std::fill_n(jsonInput, 19, 0.6f);
    float* jsonInputs[] = {jsonInput};
    float* jsonOutputs[] = {jsonOutput};
    jsonChannels.process(jsonInputs, jsonOutputs, 19);
    if (!std::all_of(std::begin(jsonOutput), std::end(jsonOutput), [](float v) { return v == 0.6f; })) {
        std::cerr << "JSON channel routing failed on a partial block\n";
        return 1;
    }
    AudioVM::Processor stereoOsc;
    stereoOsc.setup(44100.f, 0, 2, 32);
    stereoOsc.loadFromJSON(R"({"version":1,"program":[
        {"type":"oscAnalog","id":"osc","input":{"frequency":440,"unison count":4,"unison spread":20,"stereo":1}},
        {"type":"channel","id":"left","input":{"input":"osc","index":0}},
        {"type":"channel","id":"right","input":{"input":"osc","index":1}},
        {"type":"channels","id":"pair","input":["left","right"]},
        {"type":"audioOut","id":"out","input":"pair"}
    ]})");
    float stereoLeft[32]{}, stereoRight[32]{};
    float* stereoOut[] = {stereoLeft, stereoRight};
    bool differs = false, audible = false;
    for (int block = 0; block < 20; ++block) {
        stereoOsc.process(nullptr, stereoOut, 32);
        for (int n = 0; n < 32; ++n) {
            differs = differs || std::abs(stereoLeft[n] - stereoRight[n]) > 1e-5f;
            audible = audible || std::abs(stereoLeft[n]) > 1e-5f;
        }
    }
    if (!differs || !audible) {
        std::cerr << "stereo analog oscillator did not produce distinct audible channels\n";
        return 1;
    }
    AudioVM::Processor monoOsc;
    monoOsc.setup(44100.f, 0, 2, 32);
    monoOsc.loadFromJSON(R"({"version":1,"program":[
        {"type":"oscAnalog","id":"osc","input":{"frequency":440}},
        {"type":"audioOut","id":"out","input":"osc"}
    ]})");
    monoOsc.process(nullptr, stereoOut, 32);
    if (!std::equal(std::begin(stereoLeft), std::end(stereoLeft), std::begin(stereoRight))) {
        std::cerr << "default mono oscillator was not duplicated to both outputs\n";
        return 1;
    }
    AudioVM::Processor dynamicStereo;
    dynamicStereo.setup(44100.f, 0, 2, 32);
    dynamicStereo.loadFromJSON(R"({"version":1,"program":[
        {"type":"control","id":"width","input":0},
        {"type":"oscAnalog","id":"osc","input":{"frequency":440,"unison count":4,"unison spread":20,"stereo":"width"}},
        {"type":"channel","id":"right","input":{"input":"osc","index":1}},
        {"type":"audioOut","id":"out","input":"osc"}
    ]})");
    for (int block = 0; block < 8; ++block) dynamicStereo.process(nullptr, stereoOut, 32);
    if (!std::equal(std::begin(stereoLeft), std::end(stereoLeft), std::begin(stereoRight))) {
        std::cerr << "zero stereo width did not center the output\n";
        return 1;
    }
    dynamicStereo.setControlValue(0, 1.f);
    bool modulated = false;
    for (int block = 0; block < 20; ++block) {
        dynamicStereo.process(nullptr, stereoOut, 32);
        for (int n = 0; n < 32; ++n)
            modulated = modulated || std::abs(stereoLeft[n] - stereoRight[n]) > 1e-5f;
    }
    if (!modulated) {
        std::cerr << "stereo width control did not modulate the output\n";
        return 1;
    }
    for (const std::string& filterType : {"filterLP", "filterBP", "filterHP"}) {
        AudioVM::Processor stereoFilter;
        stereoFilter.setup(44100.f, 0, 4, 32);
        stereoFilter.registerType("channelSources", [] { return std::make_unique<ChannelSources>(); }, "test");
        const int filterSources = stereoFilter.addOperator("channelSources");
        const int filterPair = stereoFilter.addOperator("channels", {filterSources, filterSources + 1});
        const int cutoff = stereoFilter.addControl("cutoff", 1000.f);
        const int filterReset = stereoFilter.addControl("reset", 0.f);
        const int filteredPair = stereoFilter.addOperator(filterType, {filterPair, cutoff, -1, filterReset});
        const int leftIndex = stereoFilter.addConstant(0.f);
        const int rightIndex = stereoFilter.addConstant(1.f);
        const int filteredLeft = stereoFilter.addOperator("channel", {filteredPair, leftIndex});
        const int filteredRight = stereoFilter.addOperator("channel", {filteredPair, rightIndex});
        const int monoLeft = stereoFilter.addOperator(filterType, {filterSources, cutoff, -1, filterReset});
        const int monoRight = stereoFilter.addOperator(filterType, {filterSources + 1, cutoff, -1, filterReset});
        const int comparison = stereoFilter.addOperator("channels", {filteredLeft, filteredRight, monoLeft, monoRight});
        stereoFilter.addOperator("audioOut", {comparison});
        stereoFilter.compileProgram();
        float filterOutput[4][32]{};
        float* filterOutputs[] = {filterOutput[0], filterOutput[1], filterOutput[2], filterOutput[3]};
        for (int block = 0; block < 12; ++block) {
            if (block == 4) stereoFilter.setControlValue(0, 3000.f);
            stereoFilter.setControlValue(1, block == 7 ? 1.f : 0.f);
            stereoFilter.process(nullptr, filterOutputs, block == 10 ? 17 : 32);
            for (int n = 0; n < (block == 10 ? 17 : 32); ++n) {
                if (std::abs(filterOutput[0][n] - filterOutput[2][n]) > 1e-6f ||
                    std::abs(filterOutput[1][n] - filterOutput[3][n]) > 1e-6f) {
                    std::cerr << filterType << " stereo output did not match independent mono filters\n";
                    return 1;
                }
            }
        }
    }
    AudioVM::Processor stereoBuffer;
    stereoBuffer.setup(44100.f, 0, 2, 32);
    stereoBuffer.registerType("channelSources", [] { return std::make_unique<ChannelSources>(); }, "test");
    const int count = stereoBuffer.addConstant(2.f);
    const int accumulator = stereoBuffer.addOperator("buffer", {count});
    const int stereoSources = stereoBuffer.addOperator("channelSources");
    const int stereoPair = stereoBuffer.addOperator("channels", {stereoSources, stereoSources + 1});
    const int scale = stereoBuffer.addConstant(2.f);
    const int scaledPair = stereoBuffer.addOperator("mul", {stereoPair, scale});
    stereoBuffer.addOperator("iadd", {accumulator, scaledPair});
    stereoBuffer.addOperator("audioOut", {accumulator});
    stereoBuffer.compileProgram();
    for (int block = 0; block < 2; ++block) stereoBuffer.process(nullptr, stereoOut, 32);
    if (std::abs(stereoLeft[31] - 0.5f) > 1e-6f || std::abs(stereoRight[31] - 1.5f) > 1e-6f) {
        std::cerr << "stereo arithmetic or buffer accumulation lost a channel\n";
        return 1;
    }
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

    // Extrema nodes must clamp full-rate signals without requiring a
    // control-to-audio conversion.
    AudioVM::Processor extremaProcessor;
    extremaProcessor.setup(44100.f, 1, 1, 16);
    extremaProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"audioIn","id":"value"},
        {"type":"const","id":"lower","input":0},
        {"type":"max","id":"lowerBounded","input":["value","lower"]},
        {"type":"const","id":"upper","input":0.75},
        {"type":"min","id":"bounded","input":["lowerBounded","upper"]},
        {"type":"audioOut","id":"out","input":"bounded"}
    ]})");
    float extremaInput[16];
    std::fill(std::begin(extremaInput), std::end(extremaInput), -0.25f);
    float* extremaInputs[] = {extremaInput};
    float extremaOutput[16]{};
    float* extremaOutputs[] = {extremaOutput};
    extremaProcessor.process(extremaInputs, extremaOutputs, 16);
    for (const float sample : extremaOutput) {
        if (sample != 0.f) {
            std::cerr << "min/max failed to apply the lower bound\n";
            return 1;
        }
    }
    std::fill(std::begin(extremaInput), std::end(extremaInput), 2.f);
    extremaProcessor.process(extremaInputs, extremaOutputs, 16);
    for (const float sample : extremaOutput) {
        if (sample != 0.75f) {
            std::cerr << "min/max failed to apply the upper bound\n";
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
        {"type":"delay","id":"delay","input":{"input":"input","time":0.01,"max":"capacity"}},
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

    // An omitted patch blocksize uses the default of 32, independently of
    // the host buffer size passed to setup().
    AudioVM::Processor defaultBlockSizeProcessor;
    defaultBlockSizeProcessor.setup(44100.f, 1, 1, 16);
    defaultBlockSizeProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"control","id":"enabled","input":0},
        {"type":"control","id":"release","input":0.0005},
        {"type":"ifStart","id":"ifStart1","input":{"condition":"enabled","timeOut":"release"}},
        {"type":"oscNoise","id":"signal"},
        {"type":"ifEnd","id":"ifEnd1"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    float defaultBlockSizeOutput[64]{};
    float* defaultBlockSizeOutputs[] = {defaultBlockSizeOutput};
    defaultBlockSizeProcessor.setControlValue(0, 1.f);
    defaultBlockSizeProcessor.process(nullptr, defaultBlockSizeOutputs, 32);
    defaultBlockSizeProcessor.setControlValue(0, 0.f);
    defaultBlockSizeProcessor.process(nullptr, defaultBlockSizeOutputs, 64);
    for (int sample = 0; sample < 32; ++sample) {
        if (defaultBlockSizeOutput[sample] != 0.f) {
            std::cerr << "missing JSON blocksize did not default to 32\n";
            return 1;
        }
    }
    for (int sample = 32; sample < 64; ++sample) {
        if (defaultBlockSizeOutput[sample] != 0.f) {
            std::cerr << "default JSON blocksize did not split the host buffer\n";
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

    // The MIDI arpeggiator follows the transport when playing, then runs
    // its beat ramp at the current tempo while the transport is stopped.
    AudioVM::Processor freeRunningBeat;
    freeRunningBeat.setup(128.f, 1, 1, 32);
    freeRunningBeat.loadFromJSON(R"({"version":1,"program":[
        {"type":"div","id":"rate","input":["transport.bpm",60]},
        {"type":"mod","id":"hostPhase","input":["transport.beatPos",1]},
        {"type":"lfoRamp","id":"beat","input":{"rate":"rate","reset":"transport.play","reset phase":"hostPhase"}},
        {"type":"toAudio","id":"signal","input":"beat"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    freeRunningBeat.setBPM(60.f);
    freeRunningBeat.setBeatPos(0.5f);
    freeRunningBeat.setPlay(true);
    float beatOutput[32]{};
    float* beatOutputs[] = {beatOutput};
    freeRunningBeat.process(nullptr, beatOutputs, 32);
    if (std::abs(beatOutput[31] - 0.5f) > 1e-5f) {
        std::cerr << "arpeggiator beat did not follow the playing transport\n";
        return 1;
    }
    freeRunningBeat.setPlay(false);
    freeRunningBeat.process(nullptr, beatOutputs, 32);
    if (std::abs(beatOutput[31] - 0.75f) > 1e-5f) {
        std::cerr << "arpeggiator beat did not continue at the stopped transport tempo\n";
        return 1;
    }

    AudioVM::Processor restartBeat;
    restartBeat.setup(128.f, 1, 1, 32);
    restartBeat.loadFromJSON(R"({"version":1,"program":[
        {"type":"value","id":"previousGate","input":0},
        {"type":"equal","id":"idle","input":["previousGate",0]},
        {"type":"equal","id":"stopped","input":["transport.play",0]},
        {"type":"mul","id":"idleOnset","input":["idle","midi.onset"]},
        {"type":"mul","id":"restart","input":["stopped","idleOnset"]},
        {"type":"valueWrite","id":"remember","input":["previousGate","midi.gate"]},
        {"type":"max","id":"reset","input":["transport.play","restart"]},
        {"type":"mod","id":"hostPhase","input":["transport.beatPos",1]},
        {"type":"mul","id":"resetPhase","input":["hostPhase","transport.play"]},
        {"type":"div","id":"rate","input":["transport.bpm",60]},
        {"type":"lfoRamp","id":"beat","input":{"rate":"rate","reset":"reset","reset phase":"resetPhase"}},
        {"type":"toAudio","id":"signal","input":"beat"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    restartBeat.setBPM(60.f);
    restartBeat.process(nullptr, beatOutputs, 32);
    restartBeat.midiEvent(static_cast<char>(0x90), 60, 100);
    restartBeat.process(nullptr, beatOutputs, 32);
    if (std::abs(beatOutput[31]) > 1e-5f) {
        std::cerr << "idle note-on did not restart the arpeggiator\n";
        return 1;
    }
    restartBeat.midiEvent(static_cast<char>(0x90), 64, 100);
    restartBeat.process(nullptr, beatOutputs, 32);
    if (std::abs(beatOutput[31] - 0.25f) > 1e-5f) {
        std::cerr << "additional note-on restarted an active arpeggiator\n";
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
        {"type":"oscAnalog","id":"osc","input":{"note":"generator.note"}},
        {"type":"envADSR","id":"env","input":{"gate":"generator.gate","a":0.001,"d":0.1,"s":0.5,"r":0.1}},
        {"type":"mul","id":"amp","input":["osc","env"]},
        {"type":"audioOut","id":"out","input":"amp"}
    ]})";
    const char* receivingSynth = R"({"version":1,"program":[
        {"type":"oscAnalog","id":"osc","input":{"note":"midi.note"}},
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

    // A graph-generated MIDI note must reach a named MIDI input before the
    // downstream synth runs. This is the in-process equivalent of wiring an
    // arpeggiator's MIDI output into a separate synth module.
    AudioVM::Processor generatedMidiProcessor;
    generatedMidiProcessor.setup(44100.f, 1, 1, 16);
    generatedMidiProcessor.loadFromJSON(R"({"version":1,"program":[
        {"type":"control","id":"gate","input":0},
        {"type":"control","id":"note","input":67},
        {"type":"midiBuffer","id":"synthMidi"},
        {"type":"midiOut","id":"arp","input":{"gate":"gate","note":"note","velocity":100},"target":"synthMidi"},
        {"type":"oscAnalog","id":"osc","input":{"note":"synthMidi.note"}},
        {"type":"envADSR","id":"env","input":{"gate":"synthMidi.gate","a":0.001,"d":0.1,"s":0.5,"r":0.1}},
        {"type":"mul","id":"amp","input":["osc","env"]},
        {"type":"audioOut","id":"out","input":"amp"}
    ]})");
    generatedMidiProcessor.setControlValue(0, 1.f);
    float generatedMidiOutput[16]{};
    float* generatedMidiOutputs[] = {generatedMidiOutput};
    generatedMidiProcessor.process(nullptr, generatedMidiOutputs, 16);
    bool generatedMidiIsAudible = false;
    for (const float sample : generatedMidiOutput) generatedMidiIsAudible = generatedMidiIsAudible || std::abs(sample) > 1e-6f;
    if (!generatedMidiIsAudible) {
        std::cerr << "generated MIDI did not reach downstream synth\n";
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
    valueProcessor.loadFromJSON(R"({"version":1,"blocksize":16,"program":[
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
    arrayProcessor.loadFromJSON(R"({"version":1,"blocksize":16,"program":[
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
    if (arrayProcessor.getVoiceCapacity() != 12 || arrayProcessor.getActiveVoiceCount() != 2) {
        std::cerr << "polyphonic voice counts did not reflect simultaneous notes\n";
        return 1;
    }
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

    // Kit pads are a MIDI-derived event fan-in: named pad triggers retain
    // simultaneous hits without requiring a polyphonic graph region.
    AudioVM::Processor kitProcessor;
    kitProcessor.setup(44100.f, 1, 1, 16);
    kitProcessor.loadFromJSON(R"({"version":1,"blocksize":16,"program":[
        {"type":"kitPad","id":"kit.kick","midi":"midi","pad":"kick"},
        {"type":"kitPad","id":"kit.snare","midi":"midi","pad":"snare"},
        {"type":"add","id":"hits","input":["kit.kick.onset","kit.snare.onset"]},
        {"type":"toAudio","id":"signal","input":"hits"},
        {"type":"audioOut","id":"out","input":"signal"}
    ]})");
    kitProcessor.midiEvent(static_cast<char>(0x90), 48, 100);
    kitProcessor.midiEvent(static_cast<char>(0x90), 49, 96);
    float kitOutput[16]{};
    float* kitOutputs[] = {kitOutput};
    kitProcessor.process(nullptr, kitOutputs, 16);
    if (std::abs(kitOutput[15] - 2.f) > 1e-6f) {
        std::cerr << "kit pads lost simultaneous MIDI hits\n";
        return 1;
    }
    kitProcessor.process(nullptr, kitOutputs, 16);
    if (std::abs(kitOutput[15]) > 1e-6f) {
        std::cerr << "kit pad onset did not clear after one block\n";
        return 1;
    }
    return 0;
}
