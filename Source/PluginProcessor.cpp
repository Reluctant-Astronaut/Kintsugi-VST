#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace analog;

namespace
{
    constexpr float tapeBaseMs = 10.0f;    // the tape transport's fixed delay (wow swings around it)
    constexpr float maxModMs = 9.0f;       // how far wow/flutter/drift may swing either way
    constexpr float refAmp = 0.178f;       // -18 dBFS RMS sine = 0 VU, the level everything is calibrated to

    // The console's output transformer, by core material. Its saturation is driven by the line amp
    // (the Drive knob): the harder you drive, the more you hear the transformer.
    //   Nickel: fast and clean, extended top, only saturates when pushed hard
    //   Steel:  the classic console sound, a little weight and punch, gentle even harmonics
    //   Iron:   big vintage core, saturates earliest, fat low end, softer top
    struct XfmrSpec { float lowHz, satK, satAmt, bloom, even, topHz; };
    const XfmrSpec xfmrs[3] = {
        {  90.0f, 2.5f, 0.6f, 0.02f, 0.00f, 70000.0f },   // nickel
        { 140.0f, 4.0f, 1.0f, 0.07f, 0.03f, 56000.0f },   // steel
        { 200.0f, 5.0f, 1.4f, 0.14f, 0.05f, 26000.0f },   // iron
    };

    // Tape machine behaviour per speed: 7½, 15, 30 ips
    struct SpeedSpec { float bumpHz, bumpDb, lowHz, hfHz, emphDb, lowDb, wowMul, flutMul; };
    const SpeedSpec speeds[3] = {
        {  48.0f, 2.2f, 22.0f, 15500.0f, 9.0f, -3.0f, 1.30f, 1.60f },   // 7½ ips: big low bump, darker, wobblier
        {  75.0f, 1.6f, 26.0f, 23000.0f, 6.0f, -3.0f, 1.00f, 1.00f },   // 15 ips: the classic
        { 115.0f, 1.0f, 34.0f, 28000.0f, 3.5f, -3.5f, 0.70f, 0.60f },   // 30 ips: tight, open, quiet
    };

    // Drive tapers: the lower half of every knob is subtle (well under 1 % distortion at 0 VU),
    // three quarters is clearly coloured, and full tilt goes properly over the top.
    // Above half way each taper eases off a little, so full tilt is thick and obviously overdone
    // but never square-wave fizz: its harmonics keep falling away smoothly
    inline float tame (float db, float d, float cutDb)
    {
        const float s = juce::jlimit (0.0f, 1.0f, (d - 0.5f) * 2.0f);
        return db - cutDb * s * s * (3.0f - 2.0f * s);
    }
    inline float consoleGain (float d) { return dbToGain (tame (-6.0f + 30.0f * std::pow (d, 1.3f), d, 5.0f)); }   // -6 .. +19 dB into the line amp
    // The valves: 0 = ECC82 (12AU7), 1 = ECC83 (12AX7), 2 = EL84 (a push-pull power stage)
    constexpr int numValves = 3;
    inline float sagScale (int type) { return type == 2 ? 2.4f : 1.0f; }   // sag lives mostly in the power stage
    // How much headroom is left as the supply droops. A real supply can only sag so far, so the
    // squeeze levels off (at most about 8 dB) instead of piling on gain
    inline float sagHeadroom (float k) { return 1.0f / (1.0f + k / (1.0f + 0.6f * k)); }
    // The valve's input roll-off (Miller capacitance): the more gain the stage has, the lower it sits,
    // so cymbals and air are softened before they reach the saturation instead of turning to grit
    inline float millerHz (int type, float d)
    {
        switch (type)
        {
            case 0:  return 60000.0f - 40000.0f * d * d;   // ECC82: low gain, little Miller effect (down to 20 kHz)
            case 1:  return 60000.0f - 48000.0f * d * d;   // ECC83: high gain, the most (down to 12 kHz)
            default: return 60000.0f - 36000.0f * d * d;   // EL84: a pentode, its screen grid shields it (24 kHz)
        }
    }
    inline float valveGain (int type, float d)
    {
        switch (type)
        {
            case 0:  return dbToGain (tame (-30.0f + 46.0f * std::pow (d, 1.4f), d, 4.0f));   // ECC82: low gain, lots of headroom (-30 .. +12 dB)
            case 1:  return dbToGain (tame (-22.0f + 50.0f * std::pow (d, 1.4f), d, 8.0f));   // ECC83: high gain, saturates readily (-22 .. +20 dB)
            default: return dbToGain (tame (-20.0f + 44.0f * std::pow (d, 1.3f), d, 4.0f));   // EL84: power stage (-20 .. +20 dB)
        }
    }
    inline float tapeGain (float d) { return dbToGain (tame (-8.0f + 30.0f * std::pow (d, 1.3f), d, 4.0f)); }   // -8 .. +18 dB onto the tape

    // The tube's bias asymmetry (its even harmonics) grows as it's driven: a barely-driven tube is clean
    inline float warmBias (float bias, float d) { return (0.1f + 0.9f * bias) * (0.3f + 0.7f * d); }
    inline float cleanBias (float bias, float d) { return (0.06f + 0.5f * bias) * (0.3f + 0.7f * d); }   // ECC82: gentler asymmetry
    inline float gritBias (float bias, float d) { return (0.05f + 0.6f * bias) * (0.3f + 0.7f * d); }

    // Bias sets the valve's operating point, as on real gear.
    //   HOT (more idle current): less headroom, earlier and softer saturation, more compression.
    //   COLD (less idle current): more headroom, tighter and cleaner.
    // Single triodes (ECC82, ECC83) also clip more lopsidedly when hot: more even harmonics.
    // The EL84 runs push-pull, so it stays symmetrical; very cold, its two halves hand over imperfectly
    // and a faint crossover grain appears on quiet decays.
    inline float valveCurve (int type, float u, float bias, float d)
    {
        const float h = (bias - 0.5f) * 2.0f;          // -1 cold .. +1 hot
        const float hg = dbToGain (6.0f * h);          // where the valve starts to saturate
        const float v = u * hg;
        float y;
        switch (type)
        {
            case 0:  y = warmTriode (v, cleanBias (bias, d)); break;   // ECC82: soft triode, gentle evens
            case 1:  y = warmTriode (v, warmBias (bias, d));  break;   // ECC83: rich triode, strong evens
            default:                                                   // EL84 push-pull: symmetrical, round, compressing
            {
                y = 0.6f * fastTanh (v) + 0.4f * cubicSoft (v);
                // crossover: a narrow zone around zero where neither half conducts fully. It's fixed in size,
                // so it colours quiet notes and decays and all but vanishes on loud ones
                const float cross = juce::jmax (0.0f, -h - 0.3f) / 0.7f * 0.16f;   // only in the coldest third
                if (cross > 0.0f)
                {
                    constexpr float w = 0.0025f;
                    y -= cross * w * fastTanh (v / w);
                }
                break;
            }
        }
        return y / hg;   // small signals pass at unity whatever the bias
    }

    // Level compensation, so turning a drive knob changes the colour, not the loudness.
    // Each table holds the make-up gain that keeps a 0 VU sine at the same RMS level.
    struct CompTables
    {
        static constexpr int N = 33, B = 5, S = 3;
        float console[N], tape[N], valve[numValves][B][S][N];

        static float measure (const std::function<float (float)>& f)
        {
            constexpr int len = 256;
            double sumIn = 0, sumOut = 0, mean = 0;
            float out[len];
            for (int i = 0; i < len; ++i)
            {
                const float x = refAmp * std::sin (twoPi * (float) i / len);
                out[i] = f (x);
                mean += out[i];
                sumIn += x * x;
            }
            mean /= len;
            for (int i = 0; i < len; ++i) sumOut += (out[i] - mean) * (out[i] - mean);
            return (float) std::sqrt (sumIn / std::max (1e-12, sumOut));
        }

        // The valve with power-supply sag: let the sag settle on a steady 0 VU tone, then measure
        static float measureValve (int type, float bias, float sag, float d)
        {
            sag *= sagScale (type);
            const float g = valveGain (type, d);
            Follower env;
            env.set (3.0f, 120.0f, 48000.0f);
            constexpr int len = 240;   // 200 Hz at 48 kHz
            double sumIn = 0, sumOut = 0, mean = 0;
            float out[len];
            for (int cyc = 0; cyc < 60; ++cyc)
            {
                const bool last = cyc == 59;
                for (int i = 0; i < len; ++i)
                {
                    const float x = refAmp * std::sin (twoPi * (float) i / len);
                    const float h = sagHeadroom (sag * env.z);
                    const float y = h * valveCurve (type, g * x / h, bias, d) / g;
                    env.process (std::min (2.0f, std::abs (g * x)) * 0.5f);
                    if (last) { out[i] = y; mean += y; sumIn += x * x; }
                }
            }
            mean /= len;
            for (int i = 0; i < len; ++i) sumOut += (out[i] - mean) * (out[i] - mean);
            return (float) std::sqrt (sumIn / std::max (1e-12, sumOut));
        }

        CompTables()
        {
            for (int i = 0; i < N; ++i)
            {
                const float d = (float) i / (N - 1);
                console[i] = measure ([d] (float x) { const float g = consoleGain (d); return consoleCurve (g * x, 0.02f) / g; });
                tape[i]    = measure ([d] (float x) { const float g = tapeGain (d); return tapeCurve (g * x) / g; });
                for (int t = 0; t < numValves; ++t)
                    for (int b = 0; b < B; ++b)
                        for (int sg = 0; sg < S; ++sg)
                            valve[t][b][sg][i] = measureValve (t, (float) b / (B - 1), (float) sg / (S - 1) * 3.0f, d);
            }
        }

        static float lookup (const float* table, float d)
        {
            const float p = juce::jlimit (0.0f, 1.0f, d) * (N - 1);
            const int i = std::min ((int) p, N - 2);
            return table[i] + (table[i + 1] - table[i]) * (p - (float) i);
        }

        float valveComp (int type, float bias, float sag, float d) const
        {
            const float p = juce::jlimit (0.0f, 1.0f, bias) * (B - 1);
            const int b = std::min ((int) p, B - 2);
            const float q = juce::jlimit (0.0f, 1.0f, sag) * (S - 1);
            const int s = std::min ((int) q, S - 2);
            auto at = [&] (int bb, int ss) { return lookup (valve[type][bb][ss], d); };
            const float lo = at (b, s) + (at (b + 1, s) - at (b, s)) * (p - (float) b);
            const float hi = at (b, s + 1) + (at (b + 1, s + 1) - at (b, s + 1)) * (p - (float) b);
            return lo + (hi - lo) * (q - (float) s);
        }
    };

    const CompTables& comp()
    {
        static const CompTables tables;
        return tables;
    }

    // A control value ramped linearly across one chunk
    struct Ramp
    {
        float a, b;
        float at (float t) const { return a + (b - a) * t; }
    };

    Ramp ramp (juce::SmoothedValue<float>& s, int n)
    {
        const float a = s.getCurrentValue();
        s.skip (n);
        return { a, s.getCurrentValue() };
    }

    inline float delayFor (float pitchFraction, float hz, float sampleRate)
    {
        // a sine delay swing of this many samples gives this peak pitch deviation
        return pitchFraction / (twoPi * hz) * sampleRate;
    }

    juce::String pctText (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    // Factory programs
    struct Program { const char* name; std::vector<std::pair<const char*, float>> values; };
    const std::vector<Program>& programs()
    {
        static const std::vector<Program> list {
            { "Desk", {} },   // the defaults
            { "Clean Console", { { "cdrive", 0.25f }, { "xfmr", 0.0f }, { "valveon", 0.0f }, { "tapeon", 0.0f }, { "drift", 0.15f } } },
            { "Warm Tracking", { { "vdrive", 0.5f }, { "bias", 0.55f }, { "sag", 0.4f }, { "tsat", 0.55f }, { "wow", 0.15f }, { "flutter", 0.15f } } },
            { "Quarter-Inch", { { "speed", 0.0f }, { "tsat", 0.55f }, { "wow", 0.45f }, { "flutter", 0.35f }, { "wear", 0.45f },
                                { "drift", 0.5f }, { "vdrive", 0.25f } } },
            { "Bus Glue", { { "glue", 0.55f }, { "cdrive", 0.35f }, { "xfmr", 2.0f }, { "speed", 2.0f },
                            { "tsat", 0.3f }, { "wow", 0.05f }, { "flutter", 0.08f }, { "wear", 0.0f }, { "vdrive", 0.12f } } },
            { "Pushed Pentode", { { "vtype", 2.0f }, { "vdrive", 0.62f }, { "sag", 0.65f }, { "bias", 0.6f }, { "tone", 0.55f }, { "vmix", 0.85f },
                               { "tsat", 0.6f }, { "wear", 0.3f } } },
            { "Faded Memory", { { "speed", 0.0f }, { "wow", 0.7f }, { "flutter", 0.45f }, { "wear", 0.75f },
                                { "hum", 0.25f }, { "tone", 0.6f }, { "drift", 0.7f }, { "tsat", 0.45f } } },
        };
        return list;
    }

    std::atomic<int> instanceCount { 0 };
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout KintsugiAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (pctText);
    auto unit = NormalisableRange<float> (0.0f, 1.0f, 0.001f);
    auto addPct = [&] (const char* id, const char* name, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, unit, def, pct));
    };

    // Console
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "consoleon", 1 }, "Console In", StringArray { "Out", "In" }, 1));   // a two-position switch: always stores a clean value
    addPct ("cdrive",  "Console Drive", 0.30f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "xfmr", 1 }, "Transformer", StringArray { "Nickel", "Steel", "Iron" }, 1));
    addPct ("hum",     "Hum", 0.0f);
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "channel", 1 }, "Channel", 1, 24, 1,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return "No. " + String (v); })));
    addPct ("glue",    "Glue", 0.0f);    // off on tracks; turn it up on the master
    addPct ("drift",   "Instability", 0.30f);   // the unit's slow, living imperfection (internally: drift)

    // Valve
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "valveon", 1 }, "Valve In", StringArray { "Out", "In" }, 1));   // a two-position switch: always stores a clean value
    addPct ("vdrive",  "Valve Drive", 0.30f);
    addPct ("bias",    "Bias", 0.50f);
    addPct ("sag",     "Sag", 0.30f);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "vtype", 1 }, "Valve Type", StringArray { "ECC82", "ECC83", "EL84" }, 1));
    addPct ("tone",    "Valve Tone", 1.0f);
    addPct ("vmix",    "Valve Mix", 1.0f);

    // Tape
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "tapeon", 1 }, "Tape In", StringArray { "Out", "In" }, 1));   // a two-position switch: always stores a clean value
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "speed", 1 }, "Tape Speed", StringArray { "7.5 ips", "15 ips", "30 ips" }, 1));
    addPct ("tsat",    "Tape Saturation", 0.35f);
    addPct ("wow",     "Wow", 0.20f);
    addPct ("flutter", "Flutter", 0.20f);
    addPct ("wear",    "Tape Age", 0.10f);   // older tape: duller top, wandering azimuth

    // Output
    addPct ("mix", "Mix", 1.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    return { p.begin(), p.end() };
}

KintsugiAudioProcessor::KintsugiAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Kintsugi", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pConsoleOn = get ("consoleon"); pConsoleDrive = get ("cdrive"); pXfmr = get ("xfmr");
    pHum = get ("hum"); pChannel = get ("channel"); pGlue = get ("glue"); pDrift = get ("drift");
    pValveOn = get ("valveon"); pValveDrive = get ("vdrive"); pBias = get ("bias"); pSag = get ("sag");
    pValveType = get ("vtype"); pTone = get ("tone"); pValveMix = get ("vmix");
    pTapeOn = get ("tapeon"); pSpeed = get ("speed"); pTapeSat = get ("tsat");
    pWow = get ("wow"); pFlutter = get ("flutter"); pWear = get ("wear");
    pMix = get ("mix"); pOutput = get ("output");

    // Every new instance takes the next channel on the imaginary desk, so each track is a little different
    const int ch = instanceCount.fetch_add (1) % 24 + 1;
    if (auto* param = apvts.getParameter ("channel"))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) ch));

    (void) comp();   // build the level tables now rather than on the audio thread
}

bool KintsugiAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

//==============================================================================
void KintsugiAudioProcessor::applyTolerances()
{
    // Component tolerances: a fixed, repeatable personality per channel number
    const int ch = juce::roundToInt (pChannel->load());
    if (ch == appliedChannel)
        return;
    appliedChannel = ch;
    juce::Random r ((juce::int64) ch * 7919 + 17);
    auto spread = [&r] (float amount) { return (r.nextFloat() * 2.0f - 1.0f) * amount; };
    const float g = spread (0.2f), bal = spread (0.1f);
    tol.gain[0] = dbToGain (g - bal);
    tol.gain[1] = dbToGain (g + bal);
    tol.driveScale = 1.0f + spread (0.1f);
    tol.asym = spread (0.015f);
    tol.ironFreq = 1.0f + spread (0.12f);
    tol.hfFreq = 1.0f + spread (0.08f);
    tol.azimuth = spread (1.0f);
    tol.humPhase = r.nextFloat() * twoPi;
}

void KintsugiAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = std::max (chunkSize, samplesPerBlock);   // never smaller than one internal chunk
    const float fs = (float) sr;
    const float osRate = fs * (float) (1 << osOrder);

    oversampling.reset();
    oversampling.initProcessing ((size_t) maxBlock);
    const float osLatency = oversampling.getLatencyInSamples();

    // Total latency is a whole number of samples; the tape transport makes up the fraction
    totalLatency = (int) std::ceil (osLatency + tapeBaseMs * 0.001f * fs);
    tapeBaseDelay = (float) totalLatency - osLatency;
    setLatencySamples (totalLatency);

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
    transport.setMaximumDelayInSamples ((int) std::ceil ((tapeBaseMs + maxModMs + 2.0f) * 0.001f * osRate) + 16);
    transport.prepare ({ (double) osRate, (juce::uint32) (maxBlock << osOrder), 2 });
    lastMod = lastAz = 0.0f;
    dryDelay.setMaximumDelayInSamples (totalLatency + 8);
    dryDelay.prepare (spec);

    chunkBuf.setSize (2, maxBlock);
    dryBuf.setSize (2, maxBlock);

    appliedChannel = -1;
    applyTolerances();

    for (int c = 0; c < 2; ++c)
    {
        ironLP[c].setCutoff (150.0f * tol.ironFreq, osRate); ironLP[c].reset();
        transLP[c].setCutoff (60000.0f, osRate);      transLP[c].reset();
        valveDC[c].setCutoff (8.0f, osRate);          valveDC[c].reset();
        millerLP[c].setCutoff (60000.0f, osRate);     millerLP[c].reset();
        sagEnv[c].set (3.0f, 120.0f, osRate);         sagEnv[c].reset();
        glueSC[c].setCutoff (80.0f, fs);              glueSC[c].reset();
        outDC[c].setCutoff (5.0f, fs);                outDC[c].reset();

        preEmph[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (osRate, 3000.0, 0.5, 2.0f);
        deEmph[c].coefficients  = juce::dsp::IIR::Coefficients<float>::makeHighShelf (osRate, 3000.0, 0.5, 0.5f);
        headBump[c].coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, 75.0, 1.1, 1.2f);
        lowEnd[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 28.0, 0.6);
        for (auto* f : { &preEmph[c], &deEmph[c], &headBump[c], &lowEnd[c] }) f->reset();
    }
    scrapeLP.setCutoff (70.0f, fs); scrapeLP.reset();
    scrapeNoise.seed (0xabcdefu);
    appliedSpeed = -1;

    valveTone.prepare ({ (double) osRate, (juce::uint32) (maxBlock << osOrder), 2 });
    valveTone.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    tapeHF.prepare (spec);
    tapeHF.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    tapeHF.setResonance (0.707f);   // Butterworth: a flat top that rolls off only near the cutoff

    // Slow movements, each with its own seed so channels don't move in lockstep
    const uint32_t s = (uint32_t) appliedChannel * 101u;
    wowWander.prepare (fs, 0.4f, 1.3f, 11 + s);
    wowRate.prepare (fs, 2.0f, 6.0f, 12 + s);
    azWander.prepare (fs, 1.5f, 4.0f, 13 + s);
    humWobble.prepare (fs, 0.5f, 2.0f, 14 + s);
    dGain.prepare (fs, 6.0f, 14.0f, 21 + s);
    dTone.prepare (fs, 5.0f, 11.0f, 22 + s);
    dSpeed.prepare (fs, 6.0f, 15.0f, 23 + s);
    dSat.prepare (fs, 7.0f, 13.0f, 24 + s);
    dBias.prepare (fs, 4.0f, 9.0f, 25 + s);
    dBalL.prepare (fs, 5.0f, 12.0f, 26 + s);
    dBalR.prepare (fs, 5.0f, 12.0f, 27 + s);

    wowPh1 = wowPh2 = flutPh1 = flutPh2 = humPhase = 0.0;
    glueEnvDb = 0.0f;
    glueRms = 0.0f;
    heatAcc = 0.0f;


    struct Init { juce::SmoothedValue<float>* s; std::atomic<float>* p; double secs; };
    for (auto [s, p, secs] : { Init { &sConsoleDrive, pConsoleDrive, 0.05 },
                               Init { &sHum, pHum, 0.05 }, Init { &sGlue, pGlue, 0.05 }, Init { &sDrift, pDrift, 0.2 },
                               Init { &sValveDrive, pValveDrive, 0.05 }, Init { &sBias, pBias, 0.05 }, Init { &sSag, pSag, 0.05 },
                               Init { &sValveMix, pValveMix, 0.05 }, Init { &sTone, pTone, 0.05 }, Init { &sTapeSat, pTapeSat, 0.05 },
                               Init { &sWow, pWow, 0.2 }, Init { &sFlutter, pFlutter, 0.2 },
                               Init { &sWear, pWear, 0.1 }, Init { &sMix, pMix, 0.03 },
                               Init { &sConsoleOn, pConsoleOn, 0.03 }, Init { &sValveOn, pValveOn, 0.03 }, Init { &sTapeOn, pTapeOn, 0.03 } })
    {
        s->reset (sr, secs);
        s->setCurrentAndTargetValue (p->load());
    }
    sOut.reset (sr, 0.03);
    sOut.setCurrentAndTargetValue (dbToGain (pOutput->load()));
}

//==============================================================================
void KintsugiAudioProcessor::updateFilters()
{
    const int sp = juce::jlimit (0, 2, juce::roundToInt (pSpeed->load()));
    if (sp != appliedSpeed)
    {
        appliedSpeed = sp;
        const auto& spec = speeds[sp];
        const double osRate = sr * (double) (1 << osOrder);
        const float e = dbToGain (spec.emphDb);
        const auto pre  = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (osRate, 3000.0, 0.5, e);
        const auto de   = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (osRate, 3000.0, 0.5, 1.0f / e);
        const auto bump = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (sr, spec.bumpHz, 1.1, dbToGain (spec.bumpDb));
        // below the bump the playback head gently loses the lowest octave. A shelf, not a high-pass:
        // it barely shifts phase, so blending with the dry signal (Mix) never cancels the bass
        const auto low  = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sr, spec.lowHz, 0.55, dbToGain (spec.lowDb));
        for (int c = 0; c < 2; ++c)
        {
            *preEmph[c].coefficients = pre;   // assignment into existing coefficients: no allocation
            *deEmph[c].coefficients = de;
            *headBump[c].coefficients = bump;
            *lowEnd[c].coefficients = low;
        }
    }
}

void KintsugiAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    applyTolerances();
    updateFilters();

    sConsoleDrive.setTargetValue (pConsoleDrive->load());
    sHum.setTargetValue (pHum->load());
    sGlue.setTargetValue (pGlue->load());
    sDrift.setTargetValue (pDrift->load());
    sValveDrive.setTargetValue (pValveDrive->load());
    sBias.setTargetValue (pBias->load());
    sSag.setTargetValue (pSag->load());
    sValveMix.setTargetValue (pValveMix->load());
    sTone.setTargetValue (pTone->load());
    sTapeSat.setTargetValue (pTapeSat->load());
    sWow.setTargetValue (pWow->load());
    sFlutter.setTargetValue (pFlutter->load());
    sWear.setTargetValue (pWear->load());
    sMix.setTargetValue (pMix->load());
    sOut.setTargetValue (dbToGain (pOutput->load()));
    sConsoleOn.setTargetValue (pConsoleOn->load() > 0.5f ? 1.0f : 0.0f);
    sValveOn.setTargetValue (pValveOn->load() > 0.5f ? 1.0f : 0.0f);
    sTapeOn.setTargetValue (pTapeOn->load() > 0.5f ? 1.0f : 0.0f);

    double sumL = 0, sumR = 0;
    heatAcc = 0.0f;
    constexpr int chunk = chunkSize;   // controls and slow movements update this often
    for (int pos = 0; pos < numSamples; pos += chunk)
    {
        const int n = std::min (chunk, numSamples - pos);
        float* L = buffer.getWritePointer (0) + pos;
        float* R = buffer.getWritePointer (1) + pos;
        processChunk (L, R, n);
        for (int i = 0; i < n; ++i) { sumL += L[i] * L[i]; sumR += R[i] * R[i]; }
    }
    meter[0] = (float) (sumL / numSamples);
    meter[1] = (float) (sumR / numSamples);
    valveHeat = juce::jlimit (0.0f, 1.0f, heatAcc / (float) (numSamples << osOrder));
}

void KintsugiAudioProcessor::processChunk (float* L, float* R, int n)
{
    const float fs = (float) sr;
    const float osRate = fs * (float) (1 << osOrder);
    const auto& tables = comp();
    const auto& spd = speeds[appliedSpeed < 0 ? 1 : appliedSpeed];
    const int vType = juce::jlimit (0, numValves - 1, juce::roundToInt (pValveType->load()));
    const auto& xf = xfmrs[juce::jlimit (0, 2, juce::roundToInt (pXfmr->load()))];

    const Ramp cDrive = ramp (sConsoleDrive, n), hum = ramp (sHum, n),
               glue = ramp (sGlue, n), drift = ramp (sDrift, n), vDrive = ramp (sValveDrive, n), bias = ramp (sBias, n),
               sag = ramp (sSag, n), vMix = ramp (sValveMix, n), tone = ramp (sTone, n), tSat = ramp (sTapeSat, n),
               wow = ramp (sWow, n), flutter = ramp (sFlutter, n), wear = ramp (sWear, n),
               mix = ramp (sMix, n), outG = ramp (sOut, n), onC = ramp (sConsoleOn, n), onV = ramp (sValveOn, n),
               onT = ramp (sTapeOn, n);

    // ---- drift: slow wandering of gain, tone, saturation and bias (the whole unit "breathing") ----
    const float dr = drift.b;
    const float driftGainDb = 0.6f * dr * dGain.next (n);
    const float driftTone = 1.0f + 0.12f * dr * dTone.next (n);
    const float driftSatDb = 1.5f * dr * dSat.next (n);
    const float driftBias = 0.08f * dr * dBias.next (n);
    const float balL = dbToGain (0.15f * dr * dBalL.next (n)), balR = dbToGain (0.15f * dr * dBalR.next (n));

    // ---- chunk-rate filter settings ----
    {
        for (auto& f : transLP) f.setCutoff (xf.topHz, osRate);
        for (auto& f : ironLP) f.setCutoff (xf.lowHz * tol.ironFreq, osRate);
        valveTone.setCutoffFrequency (std::min (2000.0f * std::pow (10.0f, tone.b * 1.3f), osRate * 0.45f));
        tapeHF.setCutoffFrequency (std::min (spd.hfHz * tol.hfFreq * (1.0f - 0.6f * wear.b * std::sqrt (wear.b)) * driftTone, fs * 0.48f));
    }

    // Dry copy, delayed to line up with the processed signal
    float* dL = dryBuf.getWritePointer (0);
    float* dR = dryBuf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        dryDelay.pushSample (0, L[i]);
        dryDelay.pushSample (1, R[i]);
        dL[i] = dryDelay.popSample (0, (float) totalLatency);
        dR[i] = dryDelay.popSample (1, (float) totalLatency);
    }

    // ================= 1. Console input (base rate): tolerances, hum =================
    const float humA0 = hum.a > 0.0001f ? dbToGain (-96.0f + 50.0f * std::pow (hum.a, 0.7f)) : 0.0f;
    const float humA1 = hum.b > 0.0001f ? dbToGain (-96.0f + 50.0f * std::pow (hum.b, 0.7f)) : 0.0f;
    const double humInc = twoPi * 50.0 / sr;
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) n;
        const float on = onC.at (t);
        float l = L[i], r = R[i];

        // channel tolerances
        const float gl = 1.0f + on * (tol.gain[0] - 1.0f), gr = 1.0f + on * (tol.gain[1] - 1.0f);
        l *= gl; r *= gr;

        // mains hum: 50 Hz with the buzzy harmonics of a ground loop, wobbling slightly
        humPhase += humInc;
        if (humPhase > twoPi) humPhase -= twoPi;
        const float ha = (humA0 + (humA1 - humA0) * t) * on;
        if (ha > 0.0f)
        {
            const float p = (float) humPhase;
            const float w = 1.0f + 0.08f * humWobble.next();
            const float base = std::sin (p) + 0.45f * std::sin (2.0f * p + 0.3f) + 0.25f * std::sin (3.0f * p + 1.1f);
            const float buzz = 0.12f * std::sin (5.0f * p + tol.humPhase);
            l += ha * w * (base + buzz);
            r += ha * w * (base - buzz);
        }
        L[i] = l; R[i] = r;
    }

    // ================= Tape movement for this chunk (computed at base rate, applied at 4x) =================
    const float wowPct0 = 0.012f * std::pow (wow.a, 1.7f) * spd.wowMul, wowPct1 = 0.012f * std::pow (wow.b, 1.7f) * spd.wowMul;
    const float flPct0 = 0.0025f * std::pow (flutter.a, 1.5f) * spd.flutMul, flPct1 = 0.0025f * std::pow (flutter.b, 1.5f) * spd.flutMul;
    const float maxMod = maxModMs * 0.001f * fs;
    const float srScale = fs / 48000.0f;
    for (int i = 0; i <= n; ++i)
    {
        if (i == 0) { modArr[0] = lastMod; azArr[0] = lastAz; continue; }
        const float t = (float) i / (float) n;
        const float ot = onT.at (t);
        // wow (slow, irregular), flutter (fast), scrape, and slow speed drift
        const float rateMul = 1.0f + 0.15f * wowRate.next();
        wowPh1 += twoPi * 0.52 * rateMul / sr;  if (wowPh1 > twoPi) wowPh1 -= twoPi;
        wowPh2 += twoPi * 0.21 / sr;            if (wowPh2 > twoPi) wowPh2 -= twoPi;
        flutPh1 += twoPi * 7.3 / sr;            if (flutPh1 > twoPi) flutPh1 -= twoPi;
        flutPh2 += twoPi * 11.9 / sr;           if (flutPh2 > twoPi) flutPh2 -= twoPi;
        const float wp = wowPct0 + (wowPct1 - wowPct0) * t;
        const float fp = flPct0 + (flPct1 - flPct0) * t;
        const float scrape = scrapeLP.process (scrapeNoise.bi()) * 6.0f;
        float mod = delayFor (0.60f * wp, 0.52f, fs) * (float) std::sin (wowPh1)
                  + delayFor (0.25f * wp, 0.21f, fs) * (float) std::sin (wowPh2)
                  + delayFor (0.35f * wp, 0.80f, fs) * wowWander.next()
                  + delayFor (0.70f * fp, 7.3f, fs) * (float) std::sin (flutPh1)
                  + delayFor (0.30f * fp, 11.9f, fs) * (float) std::sin (flutPh2)
                  + delayFor (0.15f * fp, 60.0f, fs) * scrape
                  + delayFor (0.0012f * drift.at (t), 0.08f, fs) * dSpeed.next();
        modArr[i] = juce::jlimit (-maxMod, maxMod, mod) * ot;
        // azimuth: the playback head isn't quite square to the tape, and on worn tape it wanders
        azArr[i] = ot * (0.3f * tol.azimuth + 1.2f * wear.at (t) * azWander.next()) * srScale;
    }
    lastMod = modArr[n]; lastAz = azArr[n];

    // ================= 2. Oversampled: console amp + iron, valve, tape record + transport =================
    chunkBuf.copyFrom (0, 0, L, n);
    chunkBuf.copyFrom (1, 0, R, n);
    juce::dsp::AudioBlock<float> block (chunkBuf.getArrayOfWritePointers(), 2, (size_t) n);
    auto up = oversampling.processSamplesUp (block);
    const int un = (int) up.getNumSamples();
    float* uL = up.getChannelPointer (0);
    float* uR = up.getChannelPointer (1);

    const float satDrift = dbToGain (driftSatDb);
    const float osRateF = (float) sr * (float) (1 << osOrder);
    const float tapeDelayOS = tapeBaseDelay * (float) (1 << osOrder);
    const float asym = 0.02f + tol.asym;
    // Gains and level make-up, worked out at both ends of the chunk and glided between (cheap per sample)
    auto ends = [] (auto&& f) { return Ramp { f (0.0f), f (1.0f) }; };
    const Ramp cgR   = ends ([&] (float t) { return consoleGain (cDrive.at (t)) * tol.driveScale * satDrift; });
    const Ramp ccR   = ends ([&] (float t) { return CompTables::lookup (tables.console, cDrive.at (t)); });
    const Ramp vbR   = ends ([&] (float t) { return juce::jlimit (0.0f, 1.0f, bias.at (t) + driftBias); });
    const Ramp vgR   = ends ([&] (float t) { return valveGain (vType, vDrive.at (t)) * satDrift; });
    const Ramp vcR   = ends ([&] (float t) { return tables.valveComp (vType, vbR.at (t), sag.at (t), vDrive.at (t)); });
    const Ramp tgR   = ends ([&] (float t) { return tapeGain (tSat.at (t)) * satDrift; });
    const Ramp tcR   = ends ([&] (float t) { return CompTables::lookup (tables.tape, tSat.at (t)); });
    const bool runC = onC.a > 0.0f || onC.b > 0.0f;   // sections that are switched off are skipped
    const bool runV = onV.a > 0.0f || onV.b > 0.0f;
    const bool runT = onT.a > 0.0f || onT.b > 0.0f;

    for (int j = 0; j < un; ++j)
    {
        const float t = (float) j / (float) un;
        const float oc = onC.at (t), ov = onV.at (t), ot = onT.at (t);

        // console
        const float cg = cgR.at (t), cOutScale = ccR.at (t) / cg;
        // the transformer is pushed by the line amp: its saturation follows the Drive knob
        const float dd = cDrive.at (t);
        const float ironK = 1.0f + xf.satK * dd, ironKInv = 1.0f / ironK, ironAmt = xf.satAmt * dd * std::sqrt (dd);
        const float evenAmt = xf.even * dd;

        // valve
        const float vb = vbR.at (t);
        const float vg = vgR.at (t);
        const float vOutScale = vcR.at (t) / vg;
        const float sg = sag.at (t) * 3.0f * sagScale (vType);
        const float vm = vMix.at (t);
        if ((j & 15) == 0) for (auto& f : millerLP) f.setCutoff (millerHz (vType, vDrive.at (t)), osRateF);

        // tape transport position (in oversampled samples)
        const float bi = (float) j / (float) (1 << osOrder);
        const int i0 = std::min ((int) bi, n - 1);
        const float fr = bi - (float) i0;
        const float modOS = (modArr[i0] + (modArr[i0 + 1] - modArr[i0]) * fr) * (float) (1 << osOrder);
        const float azOS = (azArr[i0] + (azArr[i0 + 1] - azArr[i0]) * fr) * (float) (1 << osOrder);

        // tape record
        const float tg = tgR.at (t), tOutScale = tcR.at (t) / tg;

        for (int c = 0; c < 2; ++c)
        {
            float* buf = c == 0 ? uL : uR;
            float x = buf[j];

            // ---- console line amp, then its output transformer ----
            if (runC)
            {
                float y = consoleCurve (cg * x, asym) * cOutScale;
                const float lo = ironLP[c].process (y);
                // lows saturate first (the core runs out before the highs do), the low end blooms a little,
                // and steel and iron add a touch of even harmonic
                y += ironAmt * (fastTanh (lo * ironK) * ironKInv - lo) + xf.bloom * lo + evenAmt * lo * lo;
                y = transLP[c].process (y);
                x += oc * (y - x);
            }

            // ---- valve, with power-supply sag ----
            if (runV)
            {
                const float in = x;
                const float drive = vg * millerLP[c].process (in);
                // sag: after loud notes the supply droops, so the tube runs out of headroom sooner
                const float h = sagHeadroom (sg * sagEnv[c].z);
                float v = h * valveCurve (vType, drive / h, vb, vDrive.at (t));
                const float ad = std::min (2.0f, std::abs (drive));
                sagEnv[c].process (ad * 0.5f);
                heatAcc += ov * std::min (1.0f, ad * 0.8f) * 0.5f;
                v *= vOutScale;
                v = valveDC[c].process (v);
                v = valveTone.processSample (c, v);
                v = in + vm * (v - in);
                x += ov * (v - x);
            }

            // ---- tape record head: pre-emphasis, saturation, de-emphasis ----
            if (runT)
            {
                const float e = preEmph[c].processSample (x);
                const float sat = tapeCurve (tg * e) * tOutScale;
                const float d = deEmph[c].processSample (sat);
                x += ot * (d - x);
            }

            // ---- tape transport: the moving delay that carries wow and flutter (at 4x, so it stays clear) ----
            transport.pushSample (c, x);
            buf[j] = transport.popSample (c, tapeDelayOS + modOS + (c == 0 ? -0.5f : 0.5f) * azOS);
        }
    }
    oversampling.processSamplesDown (block);
    const float* cL = chunkBuf.getReadPointer (0);
    const float* cR = chunkBuf.getReadPointer (1);

    // ================= 3. Tape transport and playback, glue, output (base rate) =================
    const float glueAmt = glue.b;
    const float thr = -12.0f - 12.0f * glueAmt, ratio = 1.0f + 2.0f * glueAmt, knee = 10.0f;
    auto glueGr = [=] (float levelDb)
    {
        const float over = levelDb - thr;
        const float slope = 1.0f / ratio - 1.0f;
        if (2.0f * over < -knee) return 0.0f;
        if (2.0f * std::abs (over) <= knee) return slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        return slope * over;
    };
    const float makeup = -glueGr (-18.0f);
    const float glueAtt = 1.0f - std::exp (-1.0f / (0.010f * fs)), glueRel = 1.0f - std::exp (-1.0f / (0.250f * fs));
    const float glueAvg = 1.0f - std::exp (-1.0f / (0.012f * fs));   // RMS detector: averages over a few cycles
    const float driftG = dbToGain (driftGainDb);

    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) n;
        const float ot = onT.at (t);

        float l = cL[i], r = cR[i];

        // ---- playback: head bump, low-end loss, high-frequency loss ----
        float ch[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            float y = lowEnd[c].processSample (headBump[c].processSample (ch[c]));
            y = tapeHF.processSample (c, y);
            ch[c] += ot * (y - ch[c]);
        }
        l = ch[0]; r = ch[1];

        // ---- glue: a gentle bus compressor for the master, level-neutral at 0 VU; does nothing at 0 ----
        if (glue.a > 0.0005f || glue.b > 0.0005f)
        {
            const float bl = glueSC[0].process (l), br = glueSC[1].process (r);
            glueRms += glueAvg * (0.5f * (bl * bl + br * br) - glueRms);   // average level, so the gain doesn't ripple with the waveform
            const float levelDb = 10.0f * std::log10 (glueRms + 1e-10f);
            const float target = glueGr (levelDb);
            glueEnvDb += (target < glueEnvDb ? glueAtt : glueRel) * (target - glueEnvDb);
            const float b = onC.at (t);
            if (b > 0.0f)
            {
                const float g = dbToGain (b * (glueEnvDb + makeup));
                l *= g; r *= g;
            }
        }
        else
        {
            glueEnvDb = 0.0f;   // off: start fresh next time it's turned up
            glueRms = 0.0f;
        }

        // ---- drift, DC, mix, output ----
        l = outDC[0].process (l * driftG * balL);
        r = outDC[1].process (r * driftG * balR);
        const float m = mix.at (t), og = outG.at (t);
        L[i] = (dL[i] + m * (l - dL[i])) * og;
        R[i] = (dR[i] + m * (r - dR[i])) * og;
    }
}

//==============================================================================
int KintsugiAudioProcessor::getNumPrograms() { return (int) programs().size(); }

const juce::String KintsugiAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? juce::String (programs()[(size_t) index].name) : juce::String();
}

void KintsugiAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;
    currentProgram = index;
    // Start from the defaults (keeping this instance's channel number), then apply the program
    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (rp->getParameterID() != "channel")
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, value] : programs()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}

void KintsugiAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KintsugiAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* KintsugiAudioProcessor::createEditor()
{
    return new KintsugiAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KintsugiAudioProcessor();
}
