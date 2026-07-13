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
        SHIMMER_DELAY_PARAM,
        SHIMMER_ATTACK_PARAM,
        // Output
        VOLUME_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        AUDIO_INPUT,
        CLOCK_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        AUDIO_OUTPUT,
        GATE_CV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        GATE_LIGHT,
        LOGO_LIGHT,
        LIGHTS_LEN
    };

    struct DelayParamQuantity : ParamQuantity {
        std::string getDisplayValueString() override {
            GuitarAmp* amp = dynamic_cast<GuitarAmp*>(module);
            if (amp && amp->inputs[CLOCK_INPUT].isConnected()) {
                float val = getValue();
                int idx = clamp((int)(val * 11.999f), 0, 11);
                const char* names[] = {"1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/2D", "1/1"};
                return std::string(names[idx]);
            } else {
                return string::f("%.0f", getValue() * 3000.f);
            }
        }
        std::string getUnit() override {
            GuitarAmp* amp = dynamic_cast<GuitarAmp*>(module);
            if (amp && amp->inputs[CLOCK_INPUT].isConnected()) {
                return "";
            } else {
                return " ms";
            }
        }
    };

    // DSP objects — one per polyphonic channel (max 16 in Rack)
    static constexpr int MAX_CHANNELS = 16;
    NoiseGate gate[MAX_CHANNELS];
    Biquad eqBass[MAX_CHANNELS];
    Biquad eqMid[MAX_CHANNELS];
    Biquad eqTreble[MAX_CHANNELS];
    CabinetSim cabinet[MAX_CHANNELS];
    Shimmer shimmer[MAX_CHANNELS];

    dsp::SchmittTrigger clockTrigger[MAX_CHANNELS];
    float clockTime[MAX_CHANNELS];
    float lastClockPeriod[MAX_CHANNELS];

    float lastSampleRate = 0.f;
    dsp::ClockDivider eqDivider;
    float logoGlow = 0.f;

    GuitarAmp() {
        for (int c = 0; c < MAX_CHANNELS; c++) {
            clockTime[c] = 0.f;
            lastClockPeriod[c] = 0.f;
        }
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
        configParam<DelayParamQuantity>(SHIMMER_DELAY_PARAM, 0.f, 1.f, 0.f, "Shimmer Delay", "");
        configParam(SHIMMER_ATTACK_PARAM, 0.f, 2.f, 0.f, "Shimmer Attack", " s");

        // Output
        configParam(VOLUME_PARAM, 0.f, 2.f, 1.f, "Volume", "", 0, 100);

        configInput(AUDIO_INPUT,   "Guitar audio");
        configInput(CLOCK_INPUT,   "Clock for Shimmer Delay");
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
        float peakInput = 0.f;

        for (int c = 0; c < channels; c++) {
            float x = inputs[AUDIO_INPUT].getVoltage(c);
            peakInput = std::max(peakInput, std::abs(x));

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

            // Calculate delay samples based on clock or absolute time
            float delaySamples = 0.f;
            if (inputs[CLOCK_INPUT].isConnected()) {
                clockTime[c] += 1.f / sr;
                float cv = inputs[CLOCK_INPUT].getChannels() == 1 ? inputs[CLOCK_INPUT].getVoltage(0) : inputs[CLOCK_INPUT].getVoltage(c);
                if (clockTrigger[c].process(cv)) {
                    lastClockPeriod[c] = clockTime[c];
                    clockTime[c] = 0.f;
                }
                
                float delayParam = params[SHIMMER_DELAY_PARAM].getValue(); // 0..1
                const float multipliers[] = {0.25f, 1.f/3.f, 0.375f, 0.5f, 2.f/3.f, 0.75f, 1.0f, 4.f/3.f, 1.5f, 2.0f, 3.0f, 4.0f};
                int idx = clamp((int)(delayParam * 11.999f), 0, 11);
                float mult = multipliers[idx];
                
                delaySamples = lastClockPeriod[c] * mult * sr;
            } else {
                float delayParam = params[SHIMMER_DELAY_PARAM].getValue(); // 0..1
                delaySamples = delayParam * 3.f * sr; 
            }
            float maxDelay = sr * 3.9f;
            if (delaySamples > maxDelay) delaySamples = maxDelay;

            float shimmerAttack = params[SHIMMER_ATTACK_PARAM].getValue();

            // 5. Shimmer
            x = shimmer[c].process(x, shimmerMix, shimmerDecay, shimmerTone, delaySamples, shimmerAttack);

            // 6. Output volume
            outputs[AUDIO_OUTPUT].setVoltage(x * volume, c);
        }

        // Smoothly glow the logo based on the peak input signal (scaled to roughly 0..1 range)
        logoGlow += (peakInput / 4.f - logoGlow) * 0.005f; // very smooth response
        lights[LOGO_LIGHT].setBrightness(clamp(logoGlow, 0.f, 1.f));

        lights[GATE_LIGHT].setBrightness(anyGateOpen ? 1.f : 0.f);
    }
};

// ---------------------------------------------------------------------------
// Panel / UI
// ---------------------------------------------------------------------------

struct LogoWidget : SvgWidget {
    GuitarAmp* module;
    LogoWidget(GuitarAmp* module) {
        this->module = module;
        setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LogoLight.svg")));
    }
    void draw(const DrawArgs& args) override {
        if (!module) return;
        float b = module->lights[GuitarAmp::LOGO_LIGHT].getBrightness();
        if (b <= 0.01f) return;
        nvgSave(args.vg);
        nvgGlobalAlpha(args.vg, b);
        SvgWidget::draw(args);
        nvgRestore(args.vg);
    }
};

struct GuitarAmpWidget : ModuleWidget {
    GuitarAmpWidget(GuitarAmp* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/GuitarAmp.svg")));

        if (module) {
            LogoWidget* logo = new LogoWidget(module);
            logo->box.pos = Vec(0, 0);
            addChild(logo);
        }

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        const float Y_PORTS = 14.0f;
        const float Y_GATE = 31.0f;
        const float Y_DRIVE = 49.4f;
        const float Y_SHIMMER = 68.6f;
        const float Y_EQ = 87.3f;
        const float Y_CAB = 106.0f;

        const float d1 = 25.4f, d2 = 76.2f;
        const float e1 = 16.933f, e2 = 50.8f, e3 = 84.666f;
        const float p1 = 12.7f, p2 = 38.1f, p3 = 63.5f, p4 = 88.9f;
        const float k1 = 10.16f, k2 = 30.48f, k3 = 50.8f, k4 = 71.12f, k5 = 91.44f;

        // Ports
        addInput (createInputCentered<PJ301MPort>        (mm2px(Vec(p1, Y_PORTS)), module, GuitarAmp::AUDIO_INPUT));
        addInput (createInputCentered<PJ301MPort>        (mm2px(Vec(p2, Y_PORTS)), module, GuitarAmp::CLOCK_INPUT));
        addOutput(createOutputCentered<PJ301MPort>       (mm2px(Vec(p3, Y_PORTS)), module, GuitarAmp::GATE_CV_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>       (mm2px(Vec(p4, Y_PORTS)), module, GuitarAmp::AUDIO_OUTPUT));

        // Gate
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(p1, Y_GATE)), module, GuitarAmp::GATE_THRESH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(p2, Y_GATE)), module, GuitarAmp::GATE_ATTACK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(p3, Y_GATE)), module, GuitarAmp::GATE_RELEASE_PARAM));
        addParam(createParamCentered<CKSS>               (mm2px(Vec(p4, Y_GATE)), module, GuitarAmp::GATE_ENABLE_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(p4 + 5.0f, Y_GATE)), module, GuitarAmp::GATE_LIGHT));

        // Drive
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(d1, Y_DRIVE)), module, GuitarAmp::DRIVE_PARAM));
        addParam(createParamCentered<CKSSThree>          (mm2px(Vec(d2, Y_DRIVE)), module, GuitarAmp::DRIVE_MODE_PARAM));

        // Shimmer
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(k1, Y_SHIMMER)), module, GuitarAmp::SHIMMER_MIX_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(k2, Y_SHIMMER)), module, GuitarAmp::SHIMMER_DECAY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(k3, Y_SHIMMER)), module, GuitarAmp::SHIMMER_TONE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(k4, Y_SHIMMER)), module, GuitarAmp::SHIMMER_DELAY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(k5, Y_SHIMMER)), module, GuitarAmp::SHIMMER_ATTACK_PARAM));

        // EQ
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(e1, Y_EQ)), module, GuitarAmp::EQ_BASS_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(e2, Y_EQ)), module, GuitarAmp::EQ_MID_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(e3, Y_EQ)), module, GuitarAmp::EQ_TREBLE_PARAM));

        // Cab & Vol
        addParam(createParamCentered<CKSS>               (mm2px(Vec(e1, Y_CAB)), module, GuitarAmp::CAB_ENABLE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(e2, Y_CAB)), module, GuitarAmp::CAB_MIX_PARAM));
        addParam(createParamCentered<RoundBlackKnob>     (mm2px(Vec(e3, Y_CAB)), module, GuitarAmp::VOLUME_PARAM));
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

        const int L = NVG_ALIGN_LEFT | NVG_ALIGN_TOP;
        const int C = NVG_ALIGN_CENTER | NVG_ALIGN_TOP;
        const int R = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
        
        // Brighter gray for text
        NVGcolor textColor = nvgRGB(0xAA, 0xAA, 0xAA);

        // Helper lambda to convert mm to px for perfectly aligned text
        auto px = [](float mm) { return mm * 2.834645669f; };

        // X coordinates in px
        const float d1 = px(25.4f), d2 = px(76.2f);
        const float e1 = px(16.933f), e2 = px(50.8f), e3 = px(84.666f);
        const float p1 = px(12.7f), p2 = px(38.1f), p3 = px(63.5f), p4 = px(88.9f);
        const float k1 = px(10.16f), k2 = px(30.48f), k3 = px(50.8f), k4 = px(71.12f), k5 = px(91.44f);

        // Y coordinates for labels (slightly below the knobs)
        const float yPortsL = px(14.0f) + 16.f;
        const float yGateL = px(31.0f) + 16.f;
        const float yDriveL = px(49.4f) + 16.f;
        const float yShimmerL = px(68.6f) + 16.f;
        const float yEqL = px(87.3f) + 16.f;
        const float yCabL = px(106.0f) + 16.f;

        const float sizeTitle = 11.5f;
        const float sizeSection = 8.5f;
        const float sizeLabel = 7.5f;

        // Title
        label(px(50.8f), 3, "Guitar Amp", sizeTitle, textColor, C);

        // Box 1
        label(14, 20, "PORTS", sizeSection, textColor, L);
        label(p1, yPortsL, "IN", sizeLabel, textColor, C);
        label(p2, yPortsL, "CLK", sizeLabel, textColor, C);
        label(p3, yPortsL, "GATE", sizeLabel, textColor, C);
        label(p4, yPortsL, "OUT", sizeLabel, textColor, C);

        label(14, 66, "GATE", sizeSection, textColor, L);
        label(p1, yGateL, "THRESH", sizeLabel, textColor, C);
        label(p2, yGateL, "ATK", sizeLabel, textColor, C);
        label(p3, yGateL, "REL", sizeLabel, textColor, C);
        label(p4, yGateL, "ENABLE", sizeLabel, textColor, C);

        // Box 2
        label(14, 120, "DRIVE", sizeSection, textColor, L);
        label(d1, yDriveL, "DRIVE", sizeLabel, textColor, C);
        // Drive switch labels (aligned vertically next to the switch)
        float d2y = px(49.4f);
        label(d2 - 12, d2y - 12, "OD", sizeLabel, textColor, R);
        label(d2 - 12, d2y,      "DIS", sizeLabel, textColor, R);
        label(d2 - 12, d2y + 12, "FUZ", sizeLabel, textColor, R);

        // Box 3
        label(14, 176, "SHIMMER", sizeSection, textColor, L);
        label(k1, yShimmerL, "MIX", sizeLabel, textColor, C);
        label(k2, yShimmerL, "DECAY", sizeLabel, textColor, C);
        label(k3, yShimmerL, "TONE", sizeLabel, textColor, C);
        label(k4, yShimmerL, "DELAY", sizeLabel, textColor, C);
        label(k5, yShimmerL, "ATTACK", sizeLabel, textColor, C);

        // Box 4
        label(14, 229, "EQ", sizeSection, textColor, L);
        label(e1, yEqL, "BASS", sizeLabel, textColor, C);
        label(e2, yEqL, "MID", sizeLabel, textColor, C);
        label(e3, yEqL, "TREBLE", sizeLabel, textColor, C);

        // Box 5
        label(14, 282, "CAB · VOL", sizeSection, textColor, L);
        label(e1, yCabL, "CAB", sizeLabel, textColor, C);
        label(e2, yCabL, "MIX", sizeLabel, textColor, C);
        label(e3, yCabL, "VOL", sizeLabel, textColor, C);
    }
};

Model* modelGuitarAmp = createModel<GuitarAmp, GuitarAmpWidget>("GuitarAmpEffects");
