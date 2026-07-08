#pragma once
#include "Biquad.hpp"

// ---------------------------------------------------------------------------
// Cabinet simulator — three filters approximating a typical guitar cab
//   - Highpass  ~80 Hz  (no sub rumble)
//   - Low-mid presence peak  ~200 Hz
//   - High-mid cut         ~5 kHz  (takes off the harsh top end)
//   - Highpass rolloff     ~8 kHz  (cab doesn't reproduce very high freqs)
// ---------------------------------------------------------------------------
struct CabinetSim {
    Biquad hpf;    // remove sub-bass
    Biquad lowMid; // boost low-mid body
    Biquad hiMid;  // cut harsh high-mids
    Biquad hfCut;  // roll off highs

    void setSampleRate(float sr) {
        hpf.setHighpass(80.f, sr);
        lowMid.setPeaking(220.f, 1.2f, 4.f, sr);   // +4 dB body
        hiMid.setPeaking(4500.f, 0.8f, -6.f, sr);  // -6 dB presence cut
        hfCut.setHighShelf(7000.f, -18.f, sr);      // heavy HF rolloff
    }

    float process(float x) {
        x = hpf.process(x);
        x = lowMid.process(x);
        x = hiMid.process(x);
        x = hfCut.process(x);
        return x;
    }

    void reset() { hpf.reset(); lowMid.reset(); hiMid.reset(); hfCut.reset(); }
};
