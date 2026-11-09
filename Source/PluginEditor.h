#pragma once

#include "PluginProcessor.h"

// Shared embedded fonts
struct KintsugiFonts
{
    static juce::Font serif (float height, bool bold = true);
};

class KintsugiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KintsugiLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    void drawPointerKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    void drawCreamKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    void drawKintsugiKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    void drawBrassKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    static constexpr bool useCreamKnobs = true;   // false = the silver spun-aluminium knobs
};

// Jewel pilot light that doubles as a section's IN switch. Glow 0 = dark, 1 = fully lit (amber).
class PilotLamp : public juce::Component, public juce::SettableTooltipClient
{
public:
    PilotLamp();
    void setGlow (float);
    void paint (juce::Graphics&) override;
private:
    float glow = 0.0f;
};

// A chrome bat-handle toggle switch, as on old consoles: lever up = in, lever down = out
class ToggleSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToggleSwitch();
    void setOn (bool);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onClick;
private:
    bool on = true;
};

// Moving-coil VU meter: warm-lit cream face, black needle with real needle ballistics
class VUMeter : public juce::Component
{
public:
    // variant 0: the original meter. variant 1: the replacement, a near-twin from a different maker
    VUMeter (juce::String channelLabel, int variant);
    void setMeanSquare (float ms) { target = ms; }
    void tick (float dt);   // advance the needle physics
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void renderFace();
    juce::String label;
    int variant = 0;
    juce::Image face;
    float target = 0.0f, pos = 0.0f, vel = 0.0f;
};

// A small window cut into the plate, with a numbered drum behind it (like an old tape counter).
// The drum rolls to the new number when the knob is turned.
class DrumWindow : public juce::Component
{
public:
    explicit DrumWindow (juce::StringArray items);
    void setIndex (int i) { target = (float) juce::jlimit (0, items.size() - 1, i); }
    void snap() { pos = target; repaint(); }
    void tick();
    void paint (juce::Graphics&) override;
private:
    juce::StringArray items;
    float pos = 0.0f, target = 0.0f;
};

class KintsugiAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KintsugiAudioProcessorEditor (KintsugiAudioProcessor&);

    // The case around the plate: 0 = original cherry, 1 = oiled walnut, 2 = teak cheeks with steel rails,
    // 3 = tolex with piping and chrome corners, 4 = hammertone steel, 5 = glazed ceramic mended with gold
    static constexpr int defaultCaseStyle = 5;   // the glazed ceramic case, mended with gold
    static inline int caseStyleOverride = -1;   // for previews
    ~KintsugiAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    // The metal front plate; all controls live on it, the editor draws the wooden case around it
    struct PlateArea : public juce::Component
    {
        juce::Image* image = nullptr;
        void paint (juce::Graphics&) override;
    };
    PlateArea plateArea;
    void renderHousing();
    void renderHousingCherry();
    void renderHousingStyled (int style);
    void renderHousingCeramic();
    int caseStyle = defaultCaseStyle;
    juce::Image housing;

    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label readout;
        juce::String name;
        int steps = 0;                    // 0 = continuous, >1 = stepped switch
        bool alwaysShowValue = false;
        bool printed = true;
        std::vector<std::pair<float, juce::String>> marks;   // printed scale values
        juce::StringArray stepLabels;     // printed at the positions of a stepped switch
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& name, int steps = 0);
    Knob& addSwitch (const juce::String& paramId, const juce::String& name, juce::StringArray labels, float spread);
    void placeKnob (Knob&, float col, int row, juce::Point<int> sectionOrigin, int colWidth = 84);
    void renderPlate();
    void toggleParam (const juce::String& id);

    KintsugiAudioProcessor& proc;
    KintsugiLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *cDrive {}, *iron {}, *hum {}, *channel {}, *glue {}, *drift {};
    Knob *vDrive {}, *bias {}, *sag {}, *vType {}, *tone {}, *vMix {};
    Knob *speed {}, *tSat {}, *wow {}, *flutter {}, *wear {};
    Knob *mix {}, *output {};

    PilotLamp consoleLamp, valveLamp, tapeLamp;
    ToggleSwitch consoleSwitch, valveSwitch, tapeSwitch;
    float valveGlow = 0.0f;
    juce::Random flicker;

    DrumWindow channelDrum { [] { juce::StringArray a; for (int i = 1; i <= 24; ++i) a.add (juce::String (i)); return a; }() };
    DrumWindow xfmrDrum { { "NICKEL", "STEEL", "IRON" } };
    DrumWindow speedDrum { { juce::String::fromUTF8 ("7\xc2\xbd"), "15", "30" } };

    VUMeter vuL { "L", 0 }, vuR { "R", 0 };   // a matched pair (variant 1, a near-twin from another maker, is kept in the code)

    struct Section { juce::String title; juce::Rectangle<int> bounds; PilotLamp* lamp = nullptr; ToggleSwitch* toggle = nullptr; };
    std::vector<Section> sections;

    juce::Image plate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KintsugiAudioProcessorEditor)
};
