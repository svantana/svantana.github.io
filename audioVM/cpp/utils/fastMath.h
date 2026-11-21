#pragma once

namespace Fast {

// use this for when input is guaranteed ~[0,1]
static inline float unitSin_noWrap(float x) {
    float b = x * (1-x);
    return b * (.5f-x) * (12.63816744f + b * (35.870132f + 56.16403226f * b));
}
static inline float unitSin(float x) {
    return unitSin_noWrap(x - floorf(x));
}

} // end namespace Fast
