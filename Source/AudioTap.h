// Something that analyses the live stereo signal: the key finder, the tonal balance meter, the
// recorder. The standalone app's AudioEngine feeds every registered tap from its DSP thread; a plugin
// build would feed the same taps from processBlock. push() must be real-time safe: no locks, no
// allocation (copy into a lock-free FIFO and do the work on another thread).
#pragma once

struct AudioTap
{
    virtual ~AudioTap() = default;
    virtual void push (const float* left, const float* right, int numSamples, double sampleRate) noexcept = 0;
};
