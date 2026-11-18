#pragma once

#include "../cpp/ops/Filters.h"

void testFilterLP() {
    FilterLP::Coefs C;
    for (int iter = 0; iter < 1000; iter++) {
        FilterLP F;
        F.mSamplerate = 44100;
        F.mBlockSize = 32;
        float f = rand() * 1.f / RAND_MAX;
        float r = rand() * 4.f / RAND_MAX;
        FilterLP::Coefs T = F.getCoefs(f, r), cSteps = F.getCoefStep(C, T), Cp = C;
        for (int i = 0; i < F.mBlockSize; i++) {
            FilterLP::stepCoefs(C, T, cSteps);
            assert((C.d >= 0) && (C.d < (2/C.k - C.k/2)));
        }
    }
    
}
