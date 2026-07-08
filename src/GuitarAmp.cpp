#include "plugin.hpp"
#include <cmath>

// ---------------------------------------------------------------------------
// Includes for separated DSP blocks
// ---------------------------------------------------------------------------
#include "Biquad.hpp"
#include "CabinetSim.hpp"
#include "Drive.hpp"
#include "NoiseGate.hpp"
#include "Shimmer.hpp"

// ---------------------------------------------------------------------------
// Module definition
// ---------------------------------------------------------------------------
struct GuitarAmp : Module {
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
        // Shimmer
        SHIMMER_MIX_PARAM,
        SHIMMER_DECAY_PARAM,
        SHIMMER_TONE_PARAM,
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
    Shimmer shimmer[MAX_CHANNELS];

    float lastSampleRate = 0.f;
    dsp::ClockDivider eqDivider;

    GuitarAmp() {
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

        // Shimmer
        configParam(SHIMMER_MIX_PARAM, 0.f, 1.f, 0.f, "Shimmer Mix", "", 0, 100);
        configParam(SHIMMER_DECAY_PARAM, 0.f, 0.99f, 0.6f, "Shimmer Decay", "", 0, 100);
        configParam(SHIMMER_TONE_PARAM, 0.f, 1.f, 0.1f, "Shimmer Tone", "", 0, 100);

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
        for (int c = 0; c < MAX_CHANNELS; c++) {
            cabinet[c].setSampleRate(sr);
            shimmer[c].setSampleRate(sr);
        }
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
        float shimmerMix  = params[SHIMMER_MIX_PARAM].getValue();
        float shimmerDecay= params[SHIMMER_DECAY_PARAM].getValue();
        float shimmerTone = params[SHIMMER_TONE_PARAM].getValue();
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

            // 5. Shimmer
            x = shimmer[c].process(x, shimmerMix, shimmerDecay, shimmerTone);

            // 6. Output volume
            outputs[AUDIO_OUTPUT].setVoltage(x * volume, c);
        }

        lights[GATE_LIGHT].setBrightness(anyGateOpen ? 1.f : 0.f);
    }
};

// ---------------------------------------------------------------------------
// Panel / UI
// ---------------------------------------------------------------------------
struct GuitarAmpWidget : ModuleWidget {
    GuitarAmpWidget(GuitarAmp* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/GuitarAmp.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // 3 columns across 20 HP (101.6 mm), all rows within 128.5 mm panel height
        const float c1 = 17.f, c2 = 50.8f, c3 = 84.f;

        // Gate (y = 16 mm, 30 mm)
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c1, 16.f)), module, GuitarAmp::GATE_THRESH_PARAM));
        addParam(createParamCentered<Trimpot>            (mm2px(Vec(c2, 16.f)), module, GuitarAmp::GATE_ATTACK_PARAM));
        addParam(createParamCentered<Trimpot>            (mm2px(Vec(c3, 16.f)), module, GuitarAmp::GATE_RELEASE_PARAM));
        addParam(createParamCentered<CKSS>               (mm2px(Vec(c1, 30.f)), module, GuitarAmp::GATE_ENABLE_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(c1 + 6.f, 30.f)), module, GuitarAmp::GATE_LIGHT));

        // Drive (y = 48 mm)
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(c1, 48.f)), module, GuitarAmp::DRIVE_PARAM));
        addParam(createParamCentered<CKSSThree>          (mm2px(Vec(c3, 48.f)), module, GuitarAmp::DRIVE_MODE_PARAM));

        // Shimmer (y = 66 mm)
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c1, 66.f)), module, GuitarAmp::SHIMMER_MIX_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c2, 66.f)), module, GuitarAmp::SHIMMER_DECAY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c3, 66.f)), module, GuitarAmp::SHIMMER_TONE_PARAM));

        // EQ (y = 84 mm)
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c1, 84.f)), module, GuitarAmp::EQ_BASS_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c2, 84.f)), module, GuitarAmp::EQ_MID_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c3, 84.f)), module, GuitarAmp::EQ_TREBLE_PARAM));

        // Cabinet + Volume (y = 102 mm)
        addParam(createParamCentered<CKSS>               (mm2px(Vec(c1, 102.f)), module, GuitarAmp::CAB_ENABLE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(c2, 102.f)), module, GuitarAmp::CAB_MIX_PARAM));
        addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(c3, 102.f)), module, GuitarAmp::VOLUME_PARAM));

        // Ports (y = 120 mm)
        addInput (createInputCentered<PJ301MPort>        (mm2px(Vec(c1, 120.f)), module, GuitarAmp::AUDIO_INPUT));
        addOutput(createOutputCentered<PJ301MPort>       (mm2px(Vec(c2, 120.f)), module, GuitarAmp::GATE_CV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>       (mm2px(Vec(c3, 120.f)), module, GuitarAmp::AUDIO_OUTPUT));
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
        label(x2, 3,  "AMP & EFFECTS", 9,  nvgRGB(0xdd,0xdd,0xdd), C);

        // Gate section
        label(11, 20, "GATE",   7,  nvgRGB(0x66,0xcc,0x66), L);
        label(x1, 60, "THRESH", 6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x2, 60, "ATK",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 60, "REL",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x1, 100,"ENABLE", 6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // Drive section
        label(11, 120, "DRIVE",  7,  nvgRGB(0xcc,0x77,0x44), L);
        label(x1, 153,"DRIVE",  6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 137,"OD",     6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 153,"DIS",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 169,"FUZ",    6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // Shimmer section
        label(11, 175, "SHIMMER", 7, nvgRGB(0xdd,0x66,0xaa), L);
        label(x1, 206,"MIX",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x2, 206,"DECAY",  6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 206,"TONE",   6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // EQ section
        label(11, 228,"EQ",     7,  nvgRGB(0x44,0x88,0xcc), L);
        label(x1, 258,"BASS",   6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x2, 258,"MID",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 258,"TREBLE", 6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // Cab · Vol section
        label(11, 281,"CAB · VOL", 7, nvgRGB(0x99,0x66,0xcc), L);
        label(x1, 313,"CAB",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x2, 313,"MIX",    6,  nvgRGB(0xaa,0xaa,0xaa), C);
        label(x3, 313,"VOL",    6,  nvgRGB(0xaa,0xaa,0xaa), C);

        // Port labels
        label(x1, 368,"IN",     6,  nvgRGB(0x88,0x88,0x88), C);
        label(x2, 368,"GATE",   6,  nvgRGB(0x88,0x88,0x88), C);
        label(x3, 368,"OUT",    6,  nvgRGB(0x88,0x88,0x88), C);
    }
};

Model* modelGuitarAmp = createModel<GuitarAmp, GuitarAmpWidget>("GuitarAmpEffects");
