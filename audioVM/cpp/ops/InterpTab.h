#pragma once

namespace InterpTab {
static const int Bits = 10, KernelSize = 4, SubSteps = 1<<Bits;
static float data[SubSteps][KernelSize];
static int make() {
    int nSub = 1<<Bits;
    const float kPIf = 3.1415926535f;
    for (int i = 0; i < nSub; i++) {
        float p = i / (nSub+0.f), *tab = data[i], shape = .16f, sum = 0;
        for (int n = 0; n < KernelSize; n++) {
            float w = kPIf * (2.f * (n+1-p) / KernelSize - 1);
            tab[n] = (1 + cosf(w) + shape * (cosf(2*w) - 1)) / (KernelSize * (1-shape));
            sum += tab[n];
        }
        DEBUGCHECK(fabsf(sum-1) < 1e-5f);
    }
    return 1;
}
static int isMade = make();

inline float lookup(const float *x, int p) {
    const float *t = data[p];
    return t[0]*x[0] + t[1]*x[1] + t[2]*x[2] + t[3]*x[3];
}
inline void writePulse(float *buf, int sub_pos, float scale) {
    float *buf0 = buf + (sub_pos >> Bits);
    const float* tab = data[sub_pos & Bits];
    for (int t = 0; t < KernelSize; t++) {
        buf0[t] += scale * tab[t];
    }
}
}
