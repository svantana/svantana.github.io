#pragma once

#include <vector>

namespace AudioVM {
    struct Buffer {
        std::vector<float> buffer;
        int numSamples, numChannels;
        float value;
        float *data(int channel = 0) {
            return &buffer[numSamples * channel];
        }
        float* dataWrap(int channel = 0) {
            return &buffer[numSamples * (channel % numChannels)];
        }
        float* operator[](int i) { return data(i); }
    };

    struct Operator {
        enum PortDir { Input, Output };
        virtual int getNumPorts(PortDir dir) = 0;
        virtual std::string getPortName(PortDir dir, int index) {
            return "(unknown)";
        }
        virtual void init() {} // do setup here, if needed
        virtual void process(Buffer* inBufs, Buffer* outBufs, int numSamples) = 0;
        float mSamplerate;
    };
}
