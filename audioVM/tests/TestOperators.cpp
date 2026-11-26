#include "../cpp/AudioVM.h"
#include "../cpp/libs/picojson.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kBlockSize = 32;
constexpr int kValidationBlocks = 4;
// About 190 seconds of rendered audio gives stable wall-clock measurements
// even for the smallest operators.
constexpr int kTimingBlocks = 262144;
constexpr float kSampleRate = 44100.f;

struct OperatorDefinition {
    std::string name;
    std::vector<int> inputRates;
    std::vector<int> outputRates;
    int maxChannels;
};

struct Result {
    std::string name;
    size_t inputs;
    size_t outputs;
    std::string maxRate;
    std::vector<double> realtimeFactors;
};

std::vector<OperatorDefinition> readDefinitions(AudioVM::Processor& processor) {
    picojson::value root;
    const std::string error = picojson::parse(root, processor.getOpDefinitionsJSON());
    if (!error.empty()) throw std::runtime_error("could not parse operator definitions: " + error);

    const picojson::array& operators = root.get<picojson::object>()
        .at("operators").get<picojson::array>();
    std::vector<OperatorDefinition> definitions;
    definitions.reserve(operators.size());
    for (const picojson::value& value : operators) {
        const picojson::object& object = value.get<picojson::object>();
        OperatorDefinition definition{
            object.at("id").get<std::string>(), {}, {},
            static_cast<int>(object.at("maxChannels").get<double>())};
        for (const picojson::value& input : object.at("inputs").get<picojson::array>()) {
            definition.inputRates.push_back(static_cast<int>(input.get<picojson::object>()
                .at("rate").get<double>()));
        }
        for (const picojson::value& output : object.at("outputs").get<picojson::array>()) {
            definition.outputRates.push_back(static_cast<int>(output.get<picojson::object>()
                .at("rate").get<double>()));
        }
        definitions.push_back(std::move(definition));
    }
    return definitions;
}

int addDelay(AudioVM::Processor& processor, int fullRateInput) {
    const int delayTime = processor.addConstant(0.01f);
    const int maxDelayTime = processor.addConstant(0.25f);
    return processor.addOperator("delay", {fullRateInput, delayTime, -1, maxDelayTime});
}

int addOperatorGraph(AudioVM::Processor& processor, const OperatorDefinition& definition,
                     std::mt19937& random, int numChannels) {
    std::uniform_real_distribution<float> positive(0.1f, 0.9f);

    // Each input is fed a different non-zero random source. Full-rate ports
    // receive changing host audio; other ports get random control values.
    const int audioInput = processor.addOperator("audioIn");
    const auto controlInput = [&] { return processor.addConstant(positive(random)); };

    // These structural/stateful operations need their normal graph partners
    // in order to pass compilation, but definitions still provide their IDs.
    if (definition.name == "delayWrite") {
        const int delay = addDelay(processor, -1);
        processor.addOperator(definition.name, {delay, audioInput});
        return -1;
    }
    if (definition.name == "polyStart") {
        const int voiceCount = processor.addConstant(1.f);
        processor.addOperator(definition.name, {voiceCount, controlInput()});
        processor.addOperator("polyEnd");
        return -1;
    }
    if (definition.name == "polyEnd") {
        const int voiceCount = processor.addConstant(1.f);
        processor.addOperator("polyStart", {voiceCount, controlInput()});
        processor.addOperator(definition.name);
        return -1;
    }
    if (definition.name == "voiceKill") {
        const int voiceCount = processor.addConstant(1.f);
        const int condition = controlInput(), duration = controlInput();
        processor.addOperator("polyStart", {voiceCount});
        processor.addOperator(definition.name, {condition, duration});
        processor.addOperator("polyEnd");
        return -1;
    }
    if (definition.name == "ifStart") {
        processor.addOperator(definition.name, {controlInput(), controlInput()});
        processor.addOperator("ifEnd");
        return -1;
    }
    if (definition.name == "ifEnd") {
        processor.addOperator("ifStart", {controlInput(), controlInput()});
        processor.addOperator(definition.name);
        return -1;
    }

    if (definition.name == "const" || definition.name == "array") return processor.addConstant(positive(random));
    if (definition.name == "control") return processor.addControl("test", positive(random));
    if (definition.name == "audioOut") {
        processor.addOperator("audioOut", {audioInput});
        return audioInput;
    }
    if (definition.name == "channel") {
        return processor.addOperator("channel", {audioInput, processor.addConstant(0.f)});
    }
    if (definition.name == "channels") {
        return processor.addOperator("channels", std::vector<int>(numChannels, audioInput));
    }
    if (definition.name == "oscAnalog") {
        std::vector<int> inputs(9, -1);
        inputs[0] = processor.addConstant(440.f);
        if (numChannels == 2) inputs[8] = processor.addConstant(1.f);
        return processor.addOperator("oscAnalog", std::move(inputs));
    }
    // These source/state operators are valid to execute in isolation, but
    // their default output is intentionally zero until the host initializes
    // their state or delivers an event.
    if (definition.name == "midiIn" || definition.name == "midiBuffer" ||
        definition.name == "arrayState" ||
        definition.name == "arrayWrite" || definition.name == "valueWrite") {
        processor.addOperator(definition.name);
        return -1;
    }
    if (definition.name == "buffer") {
        return processor.addOperator("buffer", {processor.addConstant(static_cast<float>(numChannels))});
    }

    std::vector<int> inputs;
    inputs.reserve(definition.inputRates.size());
    for (const int rate : definition.inputRates) {
        inputs.push_back(rate == AudioVM::Port::Full ? audioInput : controlInput());
    }
    if (definition.name == "delay") {
        inputs[1] = processor.addConstant(0.001f);
        inputs[5] = processor.addConstant(0.01f);
    }
    if (definition.name == "envADSR" || definition.name == "envExp") {
        inputs[0] = processor.addConstant(0.75f);
    }
    if (definition.name == "filterLP" || definition.name == "filterBP" ||
        definition.name == "filterHP" || definition.name == "filterSatLP") {
        inputs[1] = processor.addConstant(1000.f);
    }
    if (definition.name == "floor" || definition.name == "round" || definition.name == "ceil") {
        inputs[0] = processor.addConstant(1.5f);
    }
    if (definition.name == "equal") inputs[1] = inputs[0];
    if (definition.name == "less" || definition.name == "lessEqual") {
        inputs[0] = processor.addConstant(0.25f);
        inputs[1] = processor.addConstant(0.75f);
    }
    if (definition.name == "greater" || definition.name == "greaterEqual") {
        inputs[0] = processor.addConstant(0.75f);
        inputs[1] = processor.addConstant(0.25f);
    }
    if (definition.name == "iadd" || definition.name == "isub" ||
        definition.name == "imul" || definition.name == "idiv") {
        const int target = processor.addOperator("audioIn");
        inputs[0] = target;
        processor.addOperator(definition.name, std::move(inputs));
        return target;
    }
    return processor.addOperator(definition.name, std::move(inputs));
}

double testOperator(const OperatorDefinition& definition, std::mt19937& random, int numChannels) {
    AudioVM::Processor processor;
    // channels joins independent mono sources, so its host input remains
    // mono while its output expands to the channel count under test.
    const int inputChannels = definition.name == "channels" ? 1 : numChannels;
    processor.setup(kSampleRate, inputChannels, numChannels, kBlockSize);
    int outputId;
    if (definition.name == "midiOut") {
        // MIDI outputs route events to a named buffer rather than exposing a
        // signal port. Observe the destination gate through an audio sink.
        processor.loadFromJSON(R"({"version":1,"program":[
            {"type":"midiBuffer","id":"destination"},
            {"type":"midiOut","id":"sender","target":"destination","input":{"gate":1,"note":60,"velocity":100}},
            {"type":"toAudio","id":"signal","input":"destination.gate"},
            {"type":"audioOut","id":"out","input":"signal"}
        ]})");
        outputId = 0;
    } else if (definition.name == "kitPad") {
        // Kit pads require named MIDI metadata, which the low-level
        // addOperator API cannot supply. Exercise the normal JSON path.
        processor.loadFromJSON(R"({"version":1,"program":[
            {"type":"kitPad","id":"kit.kick","midi":"midi","pad":"kick"},
            {"type":"toAudio","id":"signal","input":"kit.kick.onset"},
            {"type":"audioOut","id":"out","input":"signal"}
        ]})");
        processor.midiEvent(static_cast<char>(0x90), 48, 100);
        outputId = 0;
    } else {
        outputId = addOperatorGraph(processor, definition, random, numChannels);
        if (outputId >= 0) {
            const int outputRate = definition.outputRates.empty()
                ? AudioVM::Port::Full : definition.outputRates.front();
            const int sink = outputRate == AudioVM::Port::Full
                ? outputId : processor.addOperator("toAudio", {outputId});
            processor.addOperator("audioOut", {sink});
        }
        processor.compileProgram();
    }
    std::vector<std::vector<float>> input(inputChannels, std::vector<float>(kBlockSize));
    std::vector<std::vector<float>> output(numChannels, std::vector<float>(kBlockSize));
    std::vector<float*> inputPointers(inputChannels), outputPointers(numChannels);
    for (int channel = 0; channel < inputChannels; ++channel)
        inputPointers[channel] = input[channel].data();
    for (int channel = 0; channel < numChannels; ++channel)
        outputPointers[channel] = output[channel].data();
    std::uniform_real_distribution<float> signal(0.1f, 2.f);
    // Operators with no observable output (including structural markers) are
    // still exercised above; a non-zero assertion only applies when a graph
    // can expose a signal through audioOut.
    bool hasSignal = outputId < 0 || definition.name == "buffer";
    for (int block = 0; block < kValidationBlocks; ++block) {
        for (auto& channel : input)
            for (float& sample : channel) sample = signal(random);
        processor.process(inputPointers.data(), outputPointers.data(), kBlockSize);
        for (const auto& channel : output) {
            for (const float sample : channel) {
                if (!std::isfinite(sample)) {
                    throw std::runtime_error(definition.name + " produced a non-finite output");
                }
                hasSignal = hasSignal || std::abs(sample) > 1e-6f;
            }
        }
    }
    if (!hasSignal) throw std::runtime_error(definition.name + " produced only zero output");

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kTimingBlocks; ++block) {
        processor.process(inputPointers.data(), outputPointers.data(), kBlockSize);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double renderedSeconds = static_cast<double>(kTimingBlocks * kBlockSize) / kSampleRate;
    return std::chrono::duration<double>(elapsed).count() / renderedSeconds;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const unsigned int seed = argc > 1 ? static_cast<unsigned int>(std::stoul(argv[1])) : 1;
        std::mt19937 random(seed);
        AudioVM::Processor definitionProcessor;
        const std::vector<OperatorDefinition> definitions = readDefinitions(definitionProcessor);

        std::vector<Result> results;
        results.reserve(definitions.size());
        for (const OperatorDefinition& definition : definitions) {
            std::vector<double> realtimeFactors;
            realtimeFactors.reserve(definition.maxChannels);
            for (int channels = 1; channels <= definition.maxChannels; ++channels)
                realtimeFactors.push_back(testOperator(definition, random, channels));
            const bool hasPorts = !definition.inputRates.empty() || !definition.outputRates.empty();
            const auto isFullRate = [](const std::vector<int>& rates) {
                return std::any_of(rates.begin(), rates.end(), [](const int rate) {
                    // Keep ports can preserve a full-rate input, so their
                    // maximum possible rate is full.
                    return rate == AudioVM::Port::Full || rate == AudioVM::Port::Keep;
                });
            };
            const char* maxRate = !hasPorts ? "-"
                : (isFullRate(definition.inputRates) || isFullRate(definition.outputRates))
                    ? "full" : "block";
            results.push_back({definition.name, definition.inputRates.size(), definition.outputRates.size(),
                               maxRate, std::move(realtimeFactors)});
        }
        std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
            return a.realtimeFactors.front() > b.realtimeFactors.front();
        });
        for (const Result& result : results) {
            std::cout << result.name << '\t' << result.inputs << '\t' << result.outputs << '\t'
                      << result.maxRate;
            for (const double realtimeFactor : result.realtimeFactors)
                std::cout << '\t' << std::fixed << std::setprecision(0)
                          << (1'000'000.0 * realtimeFactor);
            std::cout << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "operator test failed: " << error.what() << '\n';
        return 1;
    }
}
