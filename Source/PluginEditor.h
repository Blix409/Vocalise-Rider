#pragma once
#include "PluginProcessor.h"

// Plugin screen:
//  - level graph: ORIGINAL (grey) vs AFTER RIDING (green, recalculated live from your settings),
//    with three draggable lines (LOUD, TARGET, QUIET)
//  - original vs after numbers
//  - vertical sliders: LEVELING (0-10), SILENCE, OUTPUT volume, and a stereo PEAK meter
//  - SPEED knob, SMART SILENCE, PROTECT S SOUNDS, AUTO TARGET and AUTO SET
class VocalRiderEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalRiderEditor (VocalRiderProcessor&);
    ~VocalRiderEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    // Small cog icon (top right) that opens the About panel
    class GearButton : public juce::Button
    {
    public:
        GearButton() : juce::Button ("about") { setTooltip ("About"); }
        void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    };

    // "?" icon (next to the gear) that opens the Help guide
    class HelpButton : public juce::Button
    {
    public:
        HelpButton() : juce::Button ("help") { setTooltip ("Help: what every control does"); }
        void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    };

    // Full-window Help guide: scrolls, click outside the card (or Close) to leave
    class HelpOverlay : public juce::Component
    {
    public:
        HelpOverlay();
        std::function<void()> onClose;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
    private:
        struct Content : public juce::Component
        {
            juce::TextLayout layout;
            void build (int width);
            void paint (juce::Graphics& g) override { layout.draw (g, getLocalBounds().reduced (18, 14).toFloat()); }
        };
        juce::Rectangle<int> cardR() const { return getLocalBounds().reduced (36, 30); }
        Content content;
        juce::Viewport view;
        juce::TextButton closeBtn { "Close" };
    };

    // Full-window About panel: click anywhere to close
    class AboutOverlay : public juce::Component
    {
    public:
        std::function<void()> onClose;
        juce::Image logo;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { if (onClose) onClose(); }
    };

    struct Control
    {
        juce::Label name, hint;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        juce::Label hint;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    enum class Line { none, loud, target, quiet };

    void timerCallback() override;
    void setupControl (Control&, const juce::String& title, const juce::String& hintText,
                       const juce::String& paramId, juce::Slider::SliderStyle style);
    void setupToggle (Toggle&, const juce::String& title, const juce::String& hintText, const juce::String& paramId);
    void runAutoSet();
    void commitParam (const char* id, float value);          // one-shot change (recorded by Logic's automation)

    // The target as it should be DRAWN: your setting when Auto is off (works even when Logic is stopped),
    // the learned level when Auto is on.
    float currentTarget() const { return param ("autoTarget") > 0.5f ? shownTarget : param ("target"); }
    float param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    RiderParams currentParams() const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    Line lineAt (juce::Point<int>) const;
    void setParam (const char* id, float value);

    VocalRiderProcessor& proc;
    juce::Image logo;
    GearButton gear;
    HelpButton helpBtn;
    HelpOverlay help;
    AboutOverlay about;

    Control cLevel, cSilence, cOutput, cSpeed;
    Toggle tSmart, tProtect, tAuto;
    juce::TextButton autoSetButton { "AUTO SET" };
    juce::TextButton modeBtn[3];
    juce::Label modeHint;
    juce::String autoStatus;

    static constexpr int N = RiderCore::kHistory;
    float before[N] {}, level[N] {}, sib[N] {}, gainDb[N] {}, after[N] {};

    juce::Rectangle<int> graphArea, statsArea, sideArea, meterArea, speedArea;
    float shownTarget = -22.0f;
    float statBefore[3] {}, statAfter[3] {};                  // loud, quiet, gap
    bool haveStats = false;
    Line hover = Line::none, dragging = Line::none;

    // peak meter
    float peakShown[2] { -100.0f, -100.0f }, peakHold[2] { -100.0f, -100.0f };
    int holdAge[2] { 0, 0 };
    bool clipped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalRiderEditor)
};
