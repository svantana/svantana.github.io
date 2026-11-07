#include "../cpp/AudioVM.h"
#include "../cpp/libs/picojson.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kBlockSize = 32;
// 128 blocks is only about 2.9 ms of rendered audio and makes fast operators
// dominated by timer resolution and scheduler variance. This is about 190 s
// of rendered audio, yielding a substantially more stable wall-clock sample.
constexpr int kBlocks = 262144;
constexpr float kSampleRate = 44100.f;

struct OperatorDefinition {
    std::string name;
    std::vector<int> inputRates;
};

struct Result {
    std::string name;
    double realtimeFactor;
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
        OperatorDefinition definition{object.at("id").get<std::string>(), {}};
        for (const picojson::value& input : object.at("inputs").get<picojson::array>()) {
            definition.inputRates.push_back(static_cast<int>(input.get<picojson::object>()
                .at("rate").get<double>()));
        }
        definitions.push_back(std::move(definition));
    }
    return definitions;
}

int addDelayRead(AudioVM::Processor& processor, int fullRateInput) {
    const int delayTime = processor.addConstant(0.01f);
    const int maxDelayTime = processor.addConstant(0.25f);
    return processor.addOperator("delayRead", {fullRateInput, delayTime, maxDelayTime});
}

void addOperatorGraph(AudioVM::Processor& processor, const OperatorDefinition& definition) {
    // A shared audio input provides the full-rate signal required by matching
    // ports. The constant keeps control inputs non-zero for arithmetic ops.
    const int audioInput = processor.addOperator("audioIn");
    const int controlInput = processor.addConstant(0.25f);

    // These structural/stateful operations need their normal graph partners
    // in order to pass compilation, but definitions still provide their IDs.
    if (definition.name == "delayWrite") {
        const int delayRead = addDelayRead(processor, -1);
        processor.addOperator(definition.name, {delayRead, audioInput});
        return;
    }
    if (definition.name == "polyStart") {
        const int voiceCount = processor.addConstant(1.f);
        processor.addOperator(definition.name, {voiceCount, controlInput});
        processor.addOperator("polyEnd");
        return;
    }
    if (definition.name == "polyEnd") {
        const int voiceCount = processor.addConstant(1.f);
        processor.addOperator("polyStart", {voiceCount, controlInput});
        processor.addOperator(definition.name);
        return;
    }
    if (definition.name == "ifStart") {
        processor.addOperator(definition.name, {controlInput, controlInput});
        processor.addOperator("ifEnd");
        return;
    }
    if (definition.name == "ifEnd") {
        processor.addOperator("ifStart", {controlInput, controlInput});
        processor.addOperator(definition.name);
        return;
    }

    std::vector<int> inputs;
    inputs.reserve(definition.inputRates.size());
    for (const int rate : definition.inputRates) {
        // Full and Keep inputs use the audio-rate source; Block inputs use
        // the control-rate constant.
        inputs.push_back(rate == 0 ? controlInput : audioInput);
    }
    processor.addOperator(definition.name, std::move(inputs));
}

Result benchmark(const OperatorDefinition& definition) {
    AudioVM::Processor processor;
    processor.setup(kSampleRate, 1, 1, kBlockSize);
    addOperatorGraph(processor, definition);
    processor.compileProgram();

    float input[kBlockSize];
    float output[kBlockSize]{};
    std::fill(std::begin(input), std::end(input), 0.25f);
    float* inputPointers[] = {input};
    float* outputPointers[] = {output};

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kBlocks; ++block) {
        processor.process(inputPointers, outputPointers, kBlockSize);
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double renderedSeconds = static_cast<double>(kBlocks * kBlockSize) / kSampleRate;
    return {definition.name, std::chrono::duration<double>(elapsed).count() / renderedSeconds};
}

} // namespace

int main() {
    try {
        AudioVM::Processor definitionProcessor;
        const std::vector<OperatorDefinition> definitions = readDefinitions(definitionProcessor);

        std::vector<Result> results;
        results.reserve(definitions.size());
        for (const OperatorDefinition& definition : definitions) results.push_back(benchmark(definition));
        std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
            return a.realtimeFactor > b.realtimeFactor;
        });

        size_t nameWidth = std::string("operator").size();
        for (const Result& result : results) nameWidth = std::max(nameWidth, result.name.size());
        std::cout << std::left << std::setw(static_cast<int>(nameWidth)) << "operator"
                  << "  RT factor (%)\n";
        std::cout << std::fixed << std::setprecision(5);
        for (const Result& result : results) {
            std::cout << std::left << std::setw(static_cast<int>(nameWidth)) << result.name
                      << "  " << 100.0 * result.realtimeFactor << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
