#include "../cpp/AudioVM.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    AudioVM::Processor processor;
    processor.setup(48000.f, 1, 1, 64);
    processor.loadFromJSON(R"({"version":1,"program":[
        {"type":"audioIn","id":"source"},
        {"type":"control","id":"gain","input":0.5},
        {"type":"mul","id":"scaled","input":["source","gain"]},
        {"type":"audioOut","id":"output","input":"scaled"}
    ]})");

    const std::string code = processor.getGeneratedOperatorCode("GeneratedGain");
    const auto require = [&](const std::string& text) {
        if (code.find(text) == std::string::npos) {
            std::cerr << "generated code is missing: " << text << '\n';
            return false;
        }
        return true;
    };
    if (!require("class GeneratedGain final : public AudioVM::Operator") ||
        !require("return dir == AudioVM::Port::In ? 2 : 1;") ||
        !require("in[0].data()[sample]") ||
        !require("in[1].value()") ||
        !require("(v2 * v4)") ||
        !require("out[0].data()[sample] = v5;")) {
        return 1;
    }

    AudioVM::Processor unsupported;
    unsupported.setup(48000.f, 1, 1, 64);
    unsupported.loadFromJSON(R"({"version":1,"program":[
        {"type":"oscSin","id":"osc"},
        {"type":"audioOut","id":"output","input":"osc"}
    ]})");
    try {
        unsupported.getGeneratedOperatorCode("Unsupported");
        std::cerr << "unsupported oscillator was exported\n";
        return 1;
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("oscSin") == std::string::npos) {
            std::cerr << "unsupported-node error did not name the operator\n";
            return 1;
        }
    }
    return 0;
}
