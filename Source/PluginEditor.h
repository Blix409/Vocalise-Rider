#pragma once
#include "PluginProcessor.h"

// Plugin screen (see "Vocalise - Rider GUI"):
//  - header: logo (About), legend, ? (help overlay), i (guide), cog (advanced)
//  - level graph: ORIGINAL (grey) vs AFTER RIDING (green, recalculated live from your settings) with
//    draggable TAME PEAKS / TARGET / LOWER LEVELS / SILENCE lines and a before/after numbers strip
//  - OUTPUT fader + stereo PEAK meter
//  - bottom card: SPEED / TARGET / SILENCE knobs, LIVE / WRITE / READ, SMART TARGET / SMART SILENCE / PROTECT "S"
class VocalRiderEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalRiderEditor (VocalRiderProcessor&);
    ~VocalRiderEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Used by the screenshot tool / tests: opens an overlay without clicking.
    void showHelpPage (int page);
    void showAdvanced();
    void showAbout();

private:
    //---------------------------------------------------------------- look and feel (knobs, fader, value boxes)
    class RiderLook : public juce::LookAndFeel_V4
    {
    public:
        RiderLook();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        int getSliderThumbRadius (juce::Slider&) override { return 8; }
        void drawLabel (juce::Graphics&, juce::Label&) override;
        juce::Font getLabelFont (juce::Label& l) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override;
    };

    //---------------------------------------------------------------- small custom widgets
    class IconButton : public juce::Button
    {
    public:
        enum Kind { help, info, cog };
        IconButton (Kind k, const juce::String& tip) : juce::Button ("icon"), kind (k)
        {
            setTooltip (tip);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    private:
        Kind kind;
    };

    class LogoButton : public juce::Button          // invisible: the logo and name are drawn by the editor
    {
    public:
        LogoButton() : juce::Button ("logo")
        {
            setTooltip ("About Vocalise-Rider");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paintButton (juce::Graphics&, bool, bool) override {}
    };

    class ModeButton : public juce::Button
    {
    public:
        ModeButton (const juce::String& text, juce::Colour c) : juce::Button (text), accent (c) { setButtonText (text); }
        void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
        bool active = false;
    private:
        juce::Colour accent;
    };

    class PowerToggle : public juce::ToggleButton
    {
    public:
        PowerToggle() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    };

    class StepButton : public juce::Button           // the green "-" and "+" next to a knob
    {
    public:
        explicit StepButton (bool plusSign) : juce::Button (plusSign ? "plus" : "minus"), plus (plusSign) {}
        void paintButton (juce::Graphics&, bool isOver, bool isDown) override;
    private:
        bool plus;
    };

    //---------------------------------------------------------------- overlays
    // "?" : dims the screen, outlines each control and explains it. Two pages (graph / controls).
    struct Callout
    {
        int n;
        juce::String title, body;
        juce::Colour colour;
        juce::Array<juce::Rectangle<int>> targets;   // what to outline (editor coordinates)
        int col;                                     // text box column on the page
    };

    class HelpOverlay : public juce::Component, private juce::Timer
    {
    public:
        HelpOverlay();
        std::function<void()> onClose;
        std::function<juce::Array<Callout> (int page)> getCallouts;
        void setPage (int p);
        int getPage() const { return page; }
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void visibilityChanged() override { if (isVisible()) startTimerHz (6); else stopTimer(); }
    private:
        void timerCallback() override { repaint(); }
        void layoutBoxes (const juce::Array<Callout>&, juce::Array<juce::Rectangle<int>>&, float& bodySize) const;
        int page = 0;
        juce::TextButton backBtn { "< Back" }, nextBtn { "Next >" }, closeBtn { "Close" };
    };

    class AboutOverlay : public juce::Component
    {
    public:
        AboutOverlay();
        std::function<void()> onClose, onGuide;
        juce::Image logo;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override { if (onClose) onClose(); }
    private:
        juce::TextButton guideBtn { "Open the guide" };
        juce::Rectangle<int> cardR() const { return juce::Rectangle<int> (0, 0, 460, 270).withCentre (getLocalBounds().getCentre()); }
    };

    class AdvancedOverlay : public juce::Component
    {
    public:
        explicit AdvancedOverlay (VocalRiderProcessor&);
        std::function<void()> onClose, onAutoSet;
        juce::Label status, rideNow;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override { if (! cardR().contains (e.getPosition()) && onClose) onClose(); }
    private:
        juce::Rectangle<int> cardR() const { return juce::Rectangle<int> (0, 0, 560, 262).withCentre (getLocalBounds().getCentre()); }
        struct Row
        {
            juce::Label name, desc;
            juce::Slider slider;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        };
        void setupRow (Row&, VocalRiderProcessor&, const juce::String& title, const juce::String& text, const char* id, const juce::String& suffix);
        Row leveling;
        juce::Label lookName, lookDesc;
        juce::ComboBox lookBox;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lookAtt;
        juce::TextButton autoSet { "AUTO SET" }, closeBtn { "Close" };
    };

    enum class Line { none, loud, target, quiet, silence };

    void timerCallback() override;
    void runAutoSet();
    void openGuide();
    void commitParam (const char* id, float value);          // one-shot change (recorded by Logic's automation)
    void nudge (const char* id, float delta);                // the - / + buttons
    juce::Array<Callout> buildCallouts (int page) const;

    // The target as it should be DRAWN: your setting when Smart Target is off (works even when Logic is stopped),
    // the learned level when it is on.
    float currentTarget() const { return param ("autoTarget") > 0.5f ? shownTarget : param ("target"); }
    float param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    RiderParams currentParams() const;
    int lookaheadFrames() const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    Line lineAt (juce::Point<int>) const;
    void setParam (const char* id, float value);
    juce::Rectangle<float> plotRect() const;
    juce::Rectangle<int> tagRect (float y) const;

    VocalRiderProcessor& proc;
    RiderLook look;                                          // declared early so it is destroyed last
    juce::TooltipWindow tooltip { this, 600 };
    juce::Image logo;

    IconButton helpBtn { IconButton::help, "Help: what everything does" };
    IconButton infoBtn { IconButton::info, "Open the guide" };
    IconButton cogBtn  { IconButton::cog,  "Advanced options" };
    LogoButton logoBtn;

    juce::Slider sSpeed, sTarget, sSilence, sOutput;
    juce::Label lSpeed, lTarget, lSilence;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aSpeed, aSilence, aOutput;
    StepButton spMinus { false }, spPlus { true }, tgMinus { false }, tgPlus { true }, siMinus { false }, siPlus { true };
    bool syncingTarget = false;
    bool targetGesture = false;

    ModeButton modeBtn[3] { { "LIVE MODE", juce::Colour (0xff4caf50) }, { "WRITE", juce::Colour (0xffce93d8) }, { "READ", juce::Colour (0xff80deea) } };
    PowerToggle tSmartTarget, tSmartSilence, tProtect;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aSmartTarget, aSmartSilence, aProtect;

    HelpOverlay help;
    AboutOverlay about;
    AdvancedOverlay adv;
    juce::String autoStatus;

    static constexpr int N = RiderCore::kHistory;
    float before[N] {}, level[N] {}, sib[N] {}, gainDb[N] {}, after[N] {};

    juce::Rectangle<int> graphCard, outCard, botCard, stripArea, meterArea;
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
