// Renders closed, open, closed-chokes-open, open to hat808.wav.
// Build: g++ -std=c++17 -O2 -I../Source/Core/Drums808 RenderHat.cpp -o renderhat
#include "Hat808.h"
#include <cstdio>
#include <vector>

static void writeWav (const char* path, const std::vector<float>& s, int sr)
{
    FILE* f = std::fopen (path, "wb");
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    uint32_t dataBytes = (uint32_t) s.size() * 2;
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes);
    std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (1); w16 (1);
    w32 (sr); w32 (sr * 2); w16 (2); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    for (float x : s) w16 ((uint16_t) (int16_t) (std::clamp (x, -1.0f, 1.0f) * 32767.0f));
    std::fclose (f);
}

int main()
{
    const int sr = 44100;
    boombap::Hat808 hat;
    hat.prepare (sr);

    std::vector<float> buf (sr * 2, 0.0f);
    for (int i = 0; i < (int) buf.size(); ++i)
    {
        if (i == 0)           hat.triggerClosed();
        if (i == sr / 4)      hat.triggerOpen();
        if (i == sr / 2)      hat.triggerClosed();  // chokes the open hat
        if (i == sr * 3 / 4)  hat.triggerOpen();
        buf[i] = hat.process();
    }

    float peak = 0.0f;
    for (float x : buf) peak = std::max (peak, std::abs (x));
    std::printf ("peak %.3f, active at end: %d\n", peak, (int) hat.isActive());

    writeWav ("hat808.wav", buf, sr);
}
