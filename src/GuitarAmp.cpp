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
        SHIMMER_DELAY_PARAM,
        SHIMMER_TONE_PARAM,
        SHIMMER_ATTACK_PARAM,
        // Output
        VOLUME_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        AUDIO_INPUT,
        DEPRECATED_CLOCK_INPUT,
        INPUTS_LEN
    };
    enum OutputId {
        AUDIO_OUTPUT_L,
        AUDIO_OUTPUT_R,
        GATE_CV_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        GATE_LIGHT,
        LOGO_LIGHT,
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
        configParam(SHIMMER_DECAY_PARAM, 0.f, 0.99f, 0.99f, "Shimmer Decay", "", 0, 100);
        configParam(SHIMMER_DELAY_PARAM, 0.f, 1.f, 1.f, "Shimmer Delay", "", 0, 100);
        configParam(SHIMMER_TONE_PARAM, 0.f, 1.f, 1.f, "Shimmer Tone", "", 0, 100);
        configParam(SHIMMER_ATTACK_PARAM, 0.f, 2.f, 0.4f, "Shimmer Attack", " s");

        // Output
        configParam(VOLUME_PARAM, 0.f, 2.f, 1.f, "Volume", "", 0, 100);

        configInput(AUDIO_INPUT,   "Guitar audio");
        configInput(DEPRECATED_CLOCK_INPUT, "Deprecated Clock"); // Keep ID slot for backwards compatibility
        configOutput(AUDIO_OUTPUT_L, "Processed audio L");
        configOutput(AUDIO_OUTPUT_R, "Processed audio R");
        configOutput(GATE_CV_OUTPUT, "Gate CV (10V when open)");
    }

    void onReset() override {
        for (int c = 0; c < MAX_CHANNELS; c++) {
            gate[c].reset();
            eqBass[c].reset();
            eqMid[c].reset();
            eqTreble[c].reset();
            cabinet[c].reset();
            shimmer[c].reset();
            clockTrigger[c].reset();
            clockTime[c] = 0.f;
            lastClockPeriod[c] = 0.f;
        }
        logoGlow = 0.f;
        lastSampleRate = 0.f;
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
        float volume      = params[VOLUME_PARAM].getValue();

        int channels = std::max(1, inputs[AUDIO_INPUT].getChannels());
        outputs[AUDIO_OUTPUT_L].setChannels(channels);
        outputs[AUDIO_OUTPUT_R].setChannels(channels);
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

            float shimmerDelay = params[SHIMMER_DELAY_PARAM].getValue();
            float shimmerTone = params[SHIMMER_TONE_PARAM].getValue();
            float shimmerAttack = params[SHIMMER_ATTACK_PARAM].getValue();

            // 5. Shimmer
            auto out = shimmer[c].process(x, shimmerMix, shimmerDecay, shimmerDelay, shimmerTone, shimmerAttack);

            // 6. Output volume
            outputs[AUDIO_OUTPUT_L].setVoltage(out.first * volume, c);
            outputs[AUDIO_OUTPUT_R].setVoltage(out.second * volume, c);
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

struct LogoLight : GreenLight {
    struct LogoSvgWidget : SvgWidget {
        LogoLight* light;
        void draw(const DrawArgs& args) override {}
        void drawLayer(const DrawArgs& args, int layer) override {
            if (layer == 1) {
                if (!light->module) return;
                float b = light->module->lights[light->firstLightId].getBrightness();
                if (b <= 0.01f) return;
                nvgSave(args.vg);
                nvgGlobalCompositeBlendFunc(args.vg, NVG_SRC_ALPHA, NVG_ONE); // Additive
                nvgGlobalAlpha(args.vg, b);
                SvgWidget::draw(args);

                nvgRestore(args.vg);
            }
            Widget::drawLayer(args, layer);
        }
    };

    LogoSvgWidget* sw;

    LogoLight() {
        sw = new LogoSvgWidget;
        sw->light = this;
        sw->setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LogoLight.svg")));
        this->box.size = sw->box.size;
        this->addChild(sw);
    }

    void drawBackground(const widget::Widget::DrawArgs& args) override {}
    void drawLight(const widget::Widget::DrawArgs& args) override {}
    void drawHalo(const widget::Widget::DrawArgs& args) override {}
};

struct GuitarAmpWidget : ModuleWidget {
    GuitarAmpWidget(GuitarAmp* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/GuitarAmp.svg")));

        addChild(createLight<LogoLight>(Vec(0, 0), module, GuitarAmp::LOGO_LIGHT));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        // Y coordinates (mm)
        const float Y_IO      = 43.0f;
        const float Y_GATE    = 97.5f;
        const float Y_DRIVE   = 153.0f;
        const float Y_SHIMMER = 209.0f;
        const float Y_EQ      = 266.0f;
        const float Y_CAB     = 321.0f;

        // I/O (Input, Output - Clock removed)
        addInput (createInputCentered<PJ301MPort>  (Vec(55.0f, Y_IO), module, GuitarAmp::AUDIO_INPUT));
        addOutput(createOutputCentered<PJ301MPort> (Vec(145.0f, Y_IO), module, GuitarAmp::AUDIO_OUTPUT_L));
        addOutput(createOutputCentered<PJ301MPort> (Vec(235.0f, Y_IO), module, GuitarAmp::AUDIO_OUTPUT_R));

        // GATE (Switch, Thresh, Attack, Release, Output)
        addParam(createParamCentered<CKSS>         (Vec(40.0f, Y_GATE), module, GuitarAmp::GATE_ENABLE_PARAM));
        addChild(createLightCentered<SmallLight<GreenLight>>(Vec(26.0f, Y_GATE), module, GuitarAmp::GATE_LIGHT));
        addParam(createParamCentered<RoundBlackKnob>(Vec(81.0f, Y_GATE), module, GuitarAmp::GATE_THRESH_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(136.0f, Y_GATE), module, GuitarAmp::GATE_ATTACK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(192.0f, Y_GATE), module, GuitarAmp::GATE_RELEASE_PARAM));
        addOutput(createOutputCentered<PJ301MPort>  (Vec(244.0f, Y_GATE), module, GuitarAmp::GATE_CV_OUTPUT));

        // DRIVE (Mix, Mode)
        addParam(createParamCentered<RoundBlackKnob>(Vec(80.0f, Y_DRIVE), module, GuitarAmp::DRIVE_PARAM));
        addParam(createParamCentered<CKSSThree>    (Vec(225.0f, Y_DRIVE), module, GuitarAmp::DRIVE_MODE_PARAM));

        // SHIMMER (Mix, Delay, Attack, Decay, Tone)
        addParam(createParamCentered<RoundBlackKnob>(Vec(26.0f, Y_SHIMMER), module, GuitarAmp::SHIMMER_MIX_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(81.0f, Y_SHIMMER), module, GuitarAmp::SHIMMER_DELAY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(136.0f, Y_SHIMMER), module, GuitarAmp::SHIMMER_ATTACK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(190.0f, Y_SHIMMER), module, GuitarAmp::SHIMMER_DECAY_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(244.0f, Y_SHIMMER), module, GuitarAmp::SHIMMER_TONE_PARAM));

        // EQ (Bass, Mid, Treble)
        addParam(createParamCentered<RoundBlackKnob>(Vec(55.0f, Y_EQ), module, GuitarAmp::EQ_BASS_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(142.0f, Y_EQ), module, GuitarAmp::EQ_MID_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(235.0f, Y_EQ), module, GuitarAmp::EQ_TREBLE_PARAM));

        // CAB (Switch, Mix, Vol)
        addParam(createParamCentered<CKSS>         (Vec(54.0f, Y_CAB), module, GuitarAmp::CAB_ENABLE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(144.0f, Y_CAB), module, GuitarAmp::CAB_MIX_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(Vec(235.0f, Y_CAB), module, GuitarAmp::VOLUME_PARAM));
    }
};

Model* modelGuitarAmp = createModel<GuitarAmp, GuitarAmpWidget>("GuitarAmpEffects");
