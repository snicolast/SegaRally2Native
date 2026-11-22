// Seconds from a fixed origin (QueryPerformanceCounter): one clock for the
// game thread, the window and the presenter, so their times can be compared
// (the frame log mixes them)
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

inline double qpc_seconds() {
    static const LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart) / double(freq.QuadPart);
}
