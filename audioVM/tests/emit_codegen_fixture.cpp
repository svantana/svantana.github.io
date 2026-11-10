#include "../cpp/AudioVM.h"

#include <iostream>

int main() {
    AudioVM::Processor processor;
    processor.setup(48000.f, 1, 1, 64);
    processor.loadFromJSON(R"({"version":1,"program":[
        {"type":"audioIn","id":"source"},
        {"type":"control","id":"gain","input":0.5},
        {"type":"mul","id":"scaled","input":["source","gain"]},
        {"type":"audioOut","id":"output","input":"scaled"}
    ]})");
    std::cout << processor.getGeneratedOperatorCode("GeneratedGain");
    return 0;
}
