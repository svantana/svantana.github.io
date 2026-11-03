#pragma once

#include <vector>

namespace AudioVM {
    struct Buffer {
        Buffer(float *aPtr = nullptr, int aNumSamples = 1, int aNumChannels = 1) {
            ptr = aPtr; numSamples = aNumSamples; numChannels = aNumChannels;
        }
        int numSamples = 1, numChannels = 1;
        float *ptr;
        float& value() { return *ptr; }
        void set(float v) { *ptr = v; }
        float *data(int channel = 0) {
            return ptr + numSamples * channel;
        }
        float* dataWrap(int channel = 0) {
            return ptr + numSamples * (channel % numChannels);
        }
        float* operator[](int i) { return data(i); }
        int size() const { return numSamples * numChannels; }
    };
    struct Port {
        enum Dir  { In, Out };
        enum Rate { Block, Full, Keep }; // Keep copies the incoming (first if multiple)
    };
    struct Operator {
        virtual ~Operator() = default;
        virtual int getNumPorts(Port::Dir dir) = 0;
        virtual std::string getPortName(Port::Dir dir, int index) {
            return "";
        }
        virtual Port::Rate getPortRate(Port::Dir dir, int index) { return Port::Keep; }
        virtual void init() {} // do setup here, if needed
        virtual void reset() {}
        virtual void process(Buffer* inBufs, Buffer* outBufs, int numSamples) = 0;
        float mSamplerate;
    };
}
