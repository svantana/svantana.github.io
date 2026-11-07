#include "../cpp/AudioVM.h"
#include "../cpp/libs/picojson.h"

#include <iostream>
#include <map>
#include <string>

namespace {

bool checkFilterDefinition(const picojson::object& definition, const std::string& id) {
    const auto checkPorts = [&](const char* direction,
                                const std::map<std::string, int>& expected) {
        const picojson::array& ports = definition.at(direction).get<picojson::array>();
        if (ports.size() != expected.size()) {
            std::cerr << id << " has an unexpected number of " << direction << " ports\n";
            return false;
        }
        for (const picojson::value& value : ports) {
            const picojson::object& port = value.get<picojson::object>();
            const std::string name = port.at("name").get<std::string>();
            const auto expectedRate = expected.find(name);
            if (expectedRate == expected.end() ||
                static_cast<int>(port.at("rate").get<double>()) != expectedRate->second) {
                std::cerr << id << " has an incorrect rate for " << direction << " port '"
                          << name << "'\n";
                return false;
            }
        }
        return true;
    };

    return checkPorts("inputs", {{"input", 1}, {"frequency", 0}, {"resonance", 0}, {"reset", 0}}) &&
           checkPorts("outputs", {{"output", 1}});
}

} // namespace

int main() {
    AudioVM::Processor processor;
    picojson::value root;
    const std::string error = picojson::parse(root, processor.getOpDefinitionsJSON());
    if (!error.empty()) {
        std::cerr << "could not parse operator definitions: " << error << '\n';
        return 1;
    }

    std::map<std::string, picojson::object> definitions;
    for (const picojson::value& value : root.get<picojson::object>().at("operators").get<picojson::array>()) {
        const picojson::object& definition = value.get<picojson::object>();
        const std::string description = definition.at("description").get<std::string>();
        if (description.empty()) {
            std::cerr << definition.at("id").get<std::string>() << " is missing a description\n";
            return 1;
        }
        definitions.emplace(definition.at("id").get<std::string>(), definition);
    }
    for (const std::string& id : {"filterLP", "filterBP", "filterHP"}) {
        const auto definition = definitions.find(id);
        if (definition == definitions.end()) {
            std::cerr << "missing " << id << " operator definition\n";
            return 1;
        }
        if (!checkFilterDefinition(definition->second, id)) return 1;
    }
    return 0;
}
