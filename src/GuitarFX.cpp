#include "plugin.hpp"
#include <cmath>

// ---------------------------------------------------------------------------
// Biquad filter — direct form II transposed
// Covers low-shelf, high-shelf, peaking, lowpass, highpass
// ---------------------------------------------------------------------------
struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.f; }

    // Low-shelf at frequency fc (Hz), gain in dB, sampleRate in Hz
    void setLowShelf(float fc, float gainDb, float sampleRate) {
        float A  = std::pow(10.f, gainDb / 40.f);
        float w0 = 2.f * M_PI * fc / sampleRate;
        float cosw = std::cos(w0);
        float sinw = std::sin(w0);
        float S  = 1.f; // shelf slope (1 = max slope)
        float alpha = sinw / 2.f * std::sqrt((A + 1.f / A) * (1.f / S - 1.f) + 2.f);

        float a0 =         (A + 1.f) + (A - 1.f) * cosw + 2.f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.f) - (A - 1.f) * cosw + 2.f * std::sqrt(A) * alpha) / a0;
        b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosw) / a0;
        b2 = A * ((A + 1.f) - (A - 1.f) * cosw - 2.f * std::sqrt(A) * alpha) / a0;
        a1 = -2.f * ((A - 1.f) + (A + 1.f) * cosw) / a0;
        a2 = ((A + 1.f) + (A - 1.f) * cosw - 2.f * std::sqrt(A) * alpha) / a0;
    }

    // High-shelf at frequency fc (Hz), gain in dB
    void setHighShelf(float fc, float gainDb, float sampleRate) {
        float A  = std::pow(10.f, gainDb / 40.f);
        float w0 = 2.f * M_PI * fc / sampleRate;
        float cosw = std::cos(w0);
        float sinw = std::sin(w0);
        float S  = 1.f;
        float alpha = sinw / 2.f * std::sqrt((A + 1.f / A) * (1.f / S - 1.f) + 2.f);

        float a0 =        (A + 1.f) - (A - 1.f) * cosw + 2.f * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.f) + (A - 1.f) * cosw + 2.f * std::sqrt(A) * alpha) / a0;
        b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosw) / a0;
        b2 = A * ((A + 1.f) + (A - 1.f) * cosw - 2.f * std::sqrt(A) * alpha) / a0;
        a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosw) / a0;
        a2 = ((A + 1.f) - (A - 1.f) * cosw - 2.f * std::sqrt(A) * alpha) / a0;
    }

    // Peaking EQ at fc (Hz), bandwidth Q, gain in dB
    void setPeaking(float fc, float Q, float gainDb, float sampleRate) {
        float A  = std::pow(10.f, gainDb / 40.f);
        float w0 = 2.f * M_PI * fc / sampleRate;
        float alpha = std::sin(w0) / (2.f * Q);

        float a0 = 1.f + alpha / A;
        b0 = (1.f + alpha * A) / a0;
        b1 = (-2.f * std::cos(w0)) / a0;
        b2 = (1.f - alpha * A) / a0;
        a1 = b1;
        a2 = (1.f - alpha / A) / a0;
    }

    // 1-pole highpass for DC blocking / gate detector
    void setHighpass(float fc, float sampleRate) {
        float w0 = 2.f * M_PI * fc / sampleRate;
        float cosw = std::cos(w0);
        float a0 = 1.f + (1.f - cosw) / 2.f;  // approximation; full biquad HP below
        // Proper 2nd-order Butterworth highpass
        float sinw = std::sin(w0);
        float alpha = sinw / (2.f * 0.7071f); // Q = 1/sqrt(2)
        float ia0 = 1.f / (1.f + alpha);
        b0 = ((1.f + cosw) / 2.f) * ia0;
        b1 = -(1.f + cosw) * ia0;
        b2 = b0;
        a1 = (-2.f * cosw) * ia0;
        a2 = (1.f - alpha) * ia0;
        (void)a0; // suppress unused warning
    }
};

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

// ---------------------------------------------------------------------------
// Distortion / Fuzz waveshaper
// ---------------------------------------------------------------------------
static float applyDrive(float x, float drive, int mode) {
    // drive: 0..1 → mapped to usable gain range per mode
    switch (mode) {
        case 0: { // Soft clip (overdrive) — tanh saturation
            float gain = 1.f + drive * 39.f; // 1x .. 40x
            return std::tanh(x * gain) / std::tanh(gain);
        }
        case 1: { // Hard clip (distortion)
            float gain = 1.f + drive * 49.f;
            float s = x * gain;
            return clamp(s, -1.f, 1.f);
        }
        case 2: { // Asymmetric fuzz — diode-style
            // Positive half: soft clip; negative half: harder clip + extra gain
            float gain = 1.f + drive * 79.f;
            float s = x * gain;
            if (s >= 0.f)
                return std::tanh(s);
            else
                return -std::pow(std::tanh(-s * 1.5f), 0.7f);
        }
        default:
            return x;
    }
}

// ---------------------------------------------------------------------------
// Simple noise gate with attack/release
// ---------------------------------------------------------------------------
struct NoiseGate {
    float envelope = 0.f;
    float gainState = 0.f;

    float process(float x, float threshold, float attackMs, float releaseMs, float sampleRate) {
        float attackCoef  = std::exp(-1.f / (sampleRate * attackMs  * 0.001f));
        float releaseCoef = std::exp(-1.f / (sampleRate * releaseMs * 0.001f));

        float level = std::abs(x);
        if (level > envelope)
            envelope = attackCoef  * envelope + (1.f - attackCoef)  * level;
        else
            envelope = releaseCoef * envelope + (1.f - releaseCoef) * level;

        float targetGain = (envelope > threshold) ? 1.f : 0.f;
        gainState = gainState + 0.001f * (targetGain - gainState); // smooth gain transitions
        return x * gainState;
    }

    void reset() { envelope = gainState = 0.f; }
};

// ---------------------------------------------------------------------------
// Module definition
// ---------------------------------------------------------------------------
struct GuitarFX : Module {
    enum ParamId {
        // Gate
        GATE_THRESH_PARAM,
        GATE_ATTACK_PARAM,
        GATE_RELEASE_PARAM,
        GATE_ENABLE_PARAM,
        // Drive
        DRIVE_PARAM,
        DRIVE_MODE_PARAM,   // 0=overdrive, 1=distortion, 2=fuzz
        // EQ
        EQ_BASS_PARAM,
        EQ_MID_PARAM,
        EQ_TREBLE_PARAM,
        // Cabinet
        CAB_ENABLE_PARAM,
        CAB_MIX_PARAM,
        // Output
        VOLUME_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        AUDIO_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        AUDIO_OUTPUT,
        GATE_CV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        GATE_LIGHT,
        LIGHTS_LEN
    };

    // DSP objects — one per polyphonic channel (max 16 in Rack)
    static constexpr int MAX_CHANNELS = 16;
    NoiseGate gate[MAX_CHANNELS];
    Biquad eqBass[MAX_CHANNELS];
    Biquad eqMid[MAX_CHANNELS];
    Biquad eqTreble[MAX_CHANNELS];
    CabinetSim cabinet[MAX_CHANNELS];

    float lastSampleRate = 0.f;
    dsp::ClockDivider eqDivider;

    GuitarFX() {
        eqDivider.setDivision(32);
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Gate
        configParam(GATE_THRESH_PARAM,  0.f,   1.f,  0.05f, "Gate threshold", "", 0, 100, 0)->displayPrecision = 2;
        configParam(GATE_ATTACK_PARAM,  0.1f, 50.f,  5.f,   "Gate attack",    "ms");
        configParam(GATE_RELEASE_PARAM, 5.f,  500.f, 100.f, "Gate release",   "ms");
        configSwitch(GATE_ENABLE_PARAM, 0.f, 1.f, 0.f, "Gate", {"Off", "On"});

        // Drive
        configParam(DRIVE_PARAM,      0.f,  1.f,  0.3f, "Drive",      "", 0, 100);
        configSwitch(DRIVE_MODE_PARAM, 0.f, 2.f,  0.f,  "Drive mode", {"Overdrive", "Distortion", "Fuzz"});

        // EQ — ±12 dB shelves/peak
        configParam(EQ_BASS_PARAM,   -12.f, 12.f, 0.f, "Bass",   "dB");
        configParam(EQ_MID_PARAM,    -12.f, 12.f, 0.f, "Mid",    "dB");
        configParam(EQ_TREBLE_PARAM, -12.f, 12.f, 0.f, "Treble", "dB");

        // Cabinet
        configSwitch(CAB_ENABLE_PARAM, 0.f, 1.f, 1.f, "Cabinet sim", {"Off", "On"});
        configParam(CAB_MIX_PARAM,     0.f, 1.f, 1.f, "Cabinet mix", "", 0, 100);

        // Output
        configParam(VOLUME_PARAM, 0.f, 2.f, 1.f, "Volume", "", 0, 100);

        configInput(AUDIO_INPUT,   "Guitar audio");
        configOutput(AUDIO_OUTPUT, "Processed audio");
        configOutput(GATE_CV_OUTPUT, "Gate CV (10V when open)");
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        lastSampleRate = 0.f; // force filter recalc
    }

    void rebuildCabinet(float sr) {
        for (int c = 0; c < MAX_CHANNELS; c++)
            cabinet[c].setSampleRate(sr);
        lastSampleRate = sr;
    }

    void rebuildEQ(float sr) {
        float bassGain   = params[EQ_BASS_PARAM].getValue();
        float midGain    = params[EQ_MID_PARAM].getValue();
        float trebleGain = params[EQ_TREBLE_PARAM].getValue();
        for (int c = 0; c < MAX_CHANNELS; c++) {
            eqBass[c].setLowShelf(250.f, bassGain, sr);
            eqMid[c].setPeaking(800.f, 1.0f, midGain, sr);
            eqTreble[c].setHighShelf(4000.f, trebleGain, sr);
        }
    }

    void process(const ProcessArgs& args) override {
        float sr = args.sampleRate;

        if (sr != lastSampleRate)
            rebuildCabinet(sr);

        // Update EQ coefficients every 32 samples so knobs are live
        if (eqDivider.process())
            rebuildEQ(sr);

        // Read params once per block (they don't change per sample)
        bool  gateEnabled = params[GATE_ENABLE_PARAM].getValue() > 0.5f;
        float gateThresh  = params[GATE_THRESH_PARAM].getValue();
        float gateAtk     = params[GATE_ATTACK_PARAM].getValue();
        float gateRel     = params[GATE_RELEASE_PARAM].getValue();
        float drive       = params[DRIVE_PARAM].getValue();
        int   driveMode   = (int)params[DRIVE_MODE_PARAM].getValue();
        bool  cabEnabled  = params[CAB_ENABLE_PARAM].getValue() > 0.5f;
        float cabMix      = params[CAB_MIX_PARAM].getValue();
        float volume      = params[VOLUME_PARAM].getValue();

        int channels = std::max(1, inputs[AUDIO_INPUT].getChannels());
        outputs[AUDIO_OUTPUT].setChannels(channels);
        outputs[GATE_CV_OUTPUT].setChannels(channels);

        bool anyGateOpen = false;

        for (int c = 0; c < channels; c++) {
            float x = inputs[AUDIO_INPUT].getVoltage(c);

            // 1. Noise gate (before drive — quieter signal, cleaner gate)
            if (gateEnabled)
                x = gate[c].process(x, gateThresh, gateAtk, gateRel, sr);

            // Gate CV output
            float gateOpen = (gate[c].envelope > gateThresh || !gateEnabled) ? 1.f : 0.f;
            outputs[GATE_CV_OUTPUT].setVoltage(gateOpen * 10.f, c);
            if (gateOpen > 0.5f) anyGateOpen = true;

            // 2. Drive / waveshaping
            // Input is in Rack's ±5V range; normalize to ±1 for waveshaper
            x = applyDrive(x / 5.f, drive, driveMode) * 5.f;

            // 3. EQ
            x = eqBass[c].process(x);
            x = eqMid[c].process(x);
            x = eqTreble[c].process(x);

            // 4. Cabinet sim (wet/dry mix)
            if (cabEnabled) {
                float wet = cabinet[c].process(x);
                x = x + cabMix * (wet - x);
            }

            // 5. Output volume
            outputs[AUDIO_OUTPUT].setVoltage(x * volume, c);
        }

        lights[GATE_LIGHT].setBrightness(anyGateOpen ? 1.f : 0.f);
    }
};

// ---------------------------------------------------------------------------
// Panel / UI
// ---------------------------------------------------------------------------
struct GuitarFXWidget : ModuleWidget {
    GuitarFXWidget(GuitarFX* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/GuitarFX.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // 3 columns across 20 HP (101.6 mm), all rows within 128.5 mm panel height
        const float c1 = 17.f, c2 = 50.8f, c3 = 84.f;

        // Gate (y = 22 mm, 40 mm)
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c1, 22.f)), module, GuitarFX::GATE_THRESH_PARAM));
        addParam(createParamCentered<Trimpot>            (mm2px(Vec(c2, 22.f)), module, GuitarFX::GATE_ATTACK_PARAM));
        addParam(createParamCentered<Trimpot>            (mm2px(Vec(c3, 22.f)), module, GuitarFX::GATE_RELEASE_PARAM));
        addParam(createParamCentered<CKSS>               (mm2px(Vec(c1, 40.f)), module, GuitarFX::GATE_ENABLE_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(c1 + 6.f, 40.f)), module, GuitarFX::GATE_LIGHT));

        // Drive (y = 62 mm)
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(c1, 62.f)), module, GuitarFX::DRIVE_PARAM));
        addParam(createParamCentered<CKSSThree>          (mm2px(Vec(c3, 62.f)), module, GuitarFX::DRIVE_MODE_PARAM));

        // EQ (y = 84 mm)
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c1, 84.f)), module, GuitarFX::EQ_BASS_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c2, 84.f)), module, GuitarFX::EQ_MID_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c3, 84.f)), module, GuitarFX::EQ_TREBLE_PARAM));

        // Cabinet + Volume (y = 105 mm)
        addParam(createParamCentered<CKSS>               (mm2px(Vec(c1, 105.f)), module, GuitarFX::CAB_ENABLE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c2, 105.f)), module, GuitarFX::CAB_MIX_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(c3, 105.f)), module, GuitarFX::VOLUME_PARAM));

        // Ports (y = 120 mm)
        addInput (createInputCentered<PJ301MPort>        (mm2px(Vec(c1, 120.f)), module, GuitarFX::AUDIO_INPUT));
        addOutput(createOutputCentered<PJ301MPort>       (mm2px(Vec(c2, 120.f)), module, GuitarFX::GATE_CV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>       (mm2px(Vec(c3, 120.f)), module, GuitarFX::AUDIO_OUTPUT));
    }

    void draw(const DrawArgs& args) override {
        ModuleWidget::draw(args);

        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/Nunito-Bold.ttf"));
        if (!font) return;

        NVGcontext* vg = args.vg;
        nvgFontFaceId(vg, font->handle);

        auto label = [&](float x, float y, const char* text, float size,
                         NVGcolor col, int align = NVG_ALIGN_CENTER | NVG_ALIGN_TOP) {
            nvgFontSize(vg, size);
            nvgTextAlign(vg, align);
            nvgFillColor(vg, col);
            nvgText(vg, x, y, text, nullptr);
        };

        // pixel x positions matching mm2px(c1/c2/c3)
        const float x1 = 50.f, x2 = 150.f, x3 = 248.f;
        const int L = NVG_ALIGN_LEFT | NVG_ALIGN_TOP;
        const int C = NVG_ALIGN_CENTER | NVG_ALIGN_TOP;

        // Title
        label(x2, 3,  "GUITAR FX", 9,  nvgRGB(0xdd,0xdd,0xdd), C);

        // Gate section
        label(11, 20, "GATE",   7,  nvgRGB(0x66,0xcc,0x66), L);
        label(x1, 74, "THRESH", 6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x2, 74, "ATK",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 74, "REL",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x1, 108,"ENABLE", 6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // Drive section
        label(11, 99, "DRIVE",  7,  nvgRGB(0xcc,0x77,0x44), L);
        label(x1, 193,"DRIVE",  6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 157,"OD",     6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 175,"DIS",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 193,"FUZ",    6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // EQ section
        label(11, 176,"EQ",     7,  nvgRGB(0x44,0x88,0xcc), L);
        label(x1, 257,"BASS",   6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x2, 257,"MID",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 257,"TREBLE", 6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // Cab · Vol section
        label(11, 276,"CAB · VOL", 7, nvgRGB(0x99,0x66,0xcc), L);
        label(x1, 319,"CAB",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x2, 319,"MIX",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 319,"VOL",    6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // Port labels
        label(x1, 368,"IN",     6,  nvgRGB(0x88,0x88,0x88), C);
        label(x2, 368,"GATE",   6,  nvgRGB(0x88,0x88,0x88), C);
        label(x3, 368,"OUT",    6,  nvgRGB(0x88,0x88,0x88), C);
    }
};

Model* modelGuitarFX = createModel<GuitarFX, GuitarFXWidget>("GuitarFX");
