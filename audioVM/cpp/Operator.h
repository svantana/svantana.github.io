#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace AudioVM {
    typedef const char* cStr;
    struct MidiEvent {
        unsigned char status;
        unsigned char data1;
        unsigned char data2;
    };
    class MidiEventFifo {
    public:
        static constexpr size_t Capacity = 256;

        bool push(MidiEvent event);
        bool pop(MidiEvent& event);
        void clear();

    private:
        std::array<MidiEvent, Capacity> mEvents{};
        std::atomic<size_t> mWrite{0};
        std::atomic<size_t> mRead{0};
    };
    struct Buffer {
        Buffer(float *aPtr = nullptr, int aNumSamples = 1, int aNumChannels = 1) {
            ptr = aPtr; numSamples = aNumSamples; numChannels = aNumChannels;
        }
        int numSamples = 1, numChannels = 1;
        float *ptr;
        // Input buffers always receive a valid pointer during compilation,
        // including disconnected ports (which point at the shared zero). The
        // connection flag therefore carries graph topology independently of
        // the signal storage.
        bool connected = false;
        bool hasData() const { return connected; }
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
        // Most outputs follow their declared rate. Operators such as lookup
        // tables can override this when a particular input determines length.
        virtual int getOutputNumSamples(int output, int blockSize,
                                        const std::vector<Buffer>& inputs) {
            const Port::Rate rate = getPortRate(Port::Out, output);
            return rate == Port::Full ? blockSize
                : rate == Port::Block ? 1
                : !inputs.empty() ? inputs[0].numSamples : 1;
        }
        virtual void init() {} // do setup here, if needed
        virtual void reset() {}
        virtual void process(Buffer* inBufs, Buffer* outBufs, int numSamples) {}
        float mSamplerate, mBlockSize;
    };
}
