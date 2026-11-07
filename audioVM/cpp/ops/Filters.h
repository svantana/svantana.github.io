class FilterLP : public Operator {
public:
    enum In  { Input, FreqHz, Resonance, Reset, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getName() { return "FilterLP"; }
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","frequency","resonance","reset"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output) ? Port::Full : Port::Block;
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    // static inline float doSVF(Coefs &C, State &S) // todo: combine lp/bp/hp
    void process(Buffer *i, Buffer *o, int N) override {
        Coefs T = getCoefs(i), C = mCoefs;
        if (i[Reset].value() > .5f) {
            mState.reset();
        }
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0], step = 1.f / o[0].numSamples;
        for (int n = 0; n < N; n++) {
            C.k += step * (T.k - C.k);
            C.d += step * (T.d - C.d);
            float hp = x[n] - S.l - C.d * S.b;
            S.b += C.k * hp;
            S.l += C.k * S.b;
            y[n] = S.l;
        }
        mState = S;
        mCoefs = C;
    }
    void reset() override {
        mState.reset();
    }
    struct State { float l, b; void reset() {l=b=0;} } mState;
    struct Coefs { float k, d; } mCoefs;
    Coefs getCoefs(Buffer *i) {
        Coefs C;
        float f = std::min(.5f, i[FreqHz].value() / mSamplerate);
        C.k = 2 * f * (3.1415f - 2.15f * 2*f);
        float d = 1 - i[Resonance].value();
        C.d = std::min(2 * d, .99f / C.k);
        return C;
    }
};

class FilterBP : public FilterLP {
public:
    std::string getName() { return "FilterBP"; }
    void process(Buffer *i, Buffer *o, int N) override {
        Coefs T = getCoefs(i), C = mCoefs;
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0], step = 1.f / o[0].numSamples;
        for (int n = 0; n < N; n++) {
            C.k += step * (T.k - C.k);
            C.d += step * (T.d - C.d);
            float hp = x[n] - S.l - C.d * S.b;
            S.b += C.k * hp;
            S.l += C.k * S.b;
            y[n] = S.b;
        }
        mState = S;
        mCoefs = C;
    }
};

class FilterHP : public FilterLP {
public:
    std::string getName() { return "FilterHP"; }
    void process(Buffer *i, Buffer *o, int N) override {
        Coefs T = getCoefs(i), C = mCoefs;
        State S = mState; // local copy
        float *x = i[Input][0], *y = o[Output][0], step = 1.f / o[0].numSamples;
        for (int n = 0; n < N; n++) {
            C.k += step * (T.k - C.k);
            C.d += step * (T.d - C.d);
            float hp = x[n] - S.l - C.d * S.b;
            S.b += C.k * hp;
            S.l += C.k * S.b;
            y[n] = hp;
        }
        mState = S;
        mCoefs = C;
    }
};

class FilterSatLP : public Operator {
public:
    enum In  { Input, FreqHz, Resonance, NumInputs };
    enum Out { Output, NumOutputs };
    std::string getPortName(Port::Dir dir, int index) override {
        cStr i[]={"input","frequency","resonance"};
        cStr o[]={"output"};
        return (dir==Port::In) ? i[index] : o[index];
    }
    Port::Rate getPortRate(Port::Dir dir, int index) override {
        return (dir==Port::Out && index==Output) ? Port::Full : Port::Block;
    }
    int getNumPorts(Port::Dir dir) override {
        return (dir == Port::Out) ? NumOutputs : NumInputs;
    }
    void process(Buffer *i, Buffer *o, int N) override {
        float f = std::min(.5f, i[FreqHz].value() / mSamplerate);
        cK = 2 * f * (3.1415f - 2.15f * 2*f);
        float d = 1 - i[Resonance].value();
        cDamp = std::min(2 * d, .99f / cK);
        float *x = i[Input][0], *y = o[Output][0];
        for (int n = 0; n < N; n++) {
            float hp = x[n] - satL - cDamp * sB;
            sB += cK * hp;
            sL += cK * sB;
            envL += .2f * (fabsf(sL) - envL);
            satL = sL * (1 + envL) / (1 + envL * (1 + envL));
            y[n] = satL;
        }
    }
    void reset() override {
        sL = sB = envL = satL = 0.f;
    }
private:
    float sL, sB, envL, satL, cK = 0.f, cDamp = 0.f;
};
