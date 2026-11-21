#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
    // Palette taken from the Vocalise app's dark theme
    const juce::Colour kBg      (0xff181818);   // --bg-primary
    const juce::Colour kPanel   (0xff212121);   // --bg-secondary
    const juce::Colour kPanel2  (0xff272727);   // --bg-tertiary
    const juce::Colour kGrid    (0xff313131);   // --border-color
    const juce::Colour kBefore  (0xff888888);   // --text-muted: original vocal
    const juce::Colour kAfter   (0xff4caf50);   // --accent green: after riding
    const juce::Colour kLoud    (0xffce93d8);   // purple badge: LOUD line
    const juce::Colour kQuiet   (0xff90caf9);   // blue badge:   QUIET line
    const juce::Colour kTarget  (0xff80deea);   // teal badge:   TARGET line
    const juce::Colour kText    (0xffe0e0e0);   // --text-main
    const juce::Colour kDim     (0xff888888);   // --text-muted
    const juce::Colour kFaint   (0xff555555);   // --text-faint
    const juce::Colour kWarn    (0xffffb454);   // meter: getting hot
    const juce::Colour kClip    (0xffe53935);   // meter: clipping

    constexpr float kDbTop = 0.0f, kDbBot = -60.0f;
    constexpr int kPlotLeft = 38, kPlotRight = 8;       // inner margins of the graph card

    void card (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (kPanel);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (kGrid);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    float percentile (std::vector<float>& v, float q)
    {
        std::sort (v.begin(), v.end());
        const auto idx = (size_t) juce::jlimit (0.0f, (float) (v.size() - 1), q * (float) (v.size() - 1));
        return v[idx];
    }

    float toDb (float lin) { return lin > 1.0e-5f ? 20.0f * std::log10 (lin) : -100.0f; }
}

VocalRiderEditor::VocalRiderEditor (VocalRiderProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setupControl (cLevel, "LEVELING", "0 = off\n10 = full", "leveling", juce::Slider::LinearVertical);
    setupControl (cSilence, "SILENCE", "holds below\nthis level", "gate", juce::Slider::LinearVertical);
    setupControl (cOutput, "OUTPUT", "main\nvolume", "output", juce::Slider::LinearVertical);
    cSilence.slider.setTextValueSuffix (" dB");
    cOutput.slider.setTextValueSuffix (" dB");

    setupControl (cSpeed, "SPEED", "", "speed", juce::Slider::RotaryHorizontalVerticalDrag);
    cSpeed.slider.setNumDecimalPlacesToDisplay (1);

    setupToggle (tSmart, "SMART SILENCE", "In gaps and breaths it eases any boost back to 0 so room noise isn't turned up.", "smartSilence");
    setupToggle (tProtect, "PROTECT S SOUNDS", "Hissy S and T sounds never move the ride, so they stay natural.", "protectS");
    setupToggle (tAuto, "AUTO TARGET", "", "autoTarget");

    autoSetButton.setColour (juce::TextButton::buttonColourId, kAfter);
    autoSetButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff101010));
    autoSetButton.onClick = [this] { runAutoSet(); };
    addAndMakeVisible (autoSetButton);

    {
        const char* names[3] = { "LIVE", "WRITE", "READ" };
        const char* tips[3]  = { "Rides the audio live. Nothing is recorded.",
                                 "Rides and writes the ride into Logic's automation (set the track to Latch or Touch, press play).",
                                 "Plays back the ride recorded in Logic's automation. Nothing is analysed." };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = modeBtn[i];
            b.setButtonText (names[i]);
            b.setTooltip (tips[i]);
            b.setClickingTogglesState (false);
            b.setColour (juce::TextButton::buttonColourId, kPanel2);
            b.setColour (juce::TextButton::buttonOnColourId, kAfter);
            b.setColour (juce::TextButton::textColourOffId, kText);
            b.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff101010));
            b.onClick = [this, i] { commitParam ("mode", (float) i); };
            addAndMakeVisible (b);
        }
        modeHint.setFont (juce::Font (11.5f));
        modeHint.setColour (juce::Label::textColourId, kDim);
        modeHint.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (modeHint);
    }

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    about.logo = logo;

    addAndMakeVisible (gear);
    gear.onClick = [this] { about.setVisible (true); about.toFront (true); };
    addAndMakeVisible (helpBtn);
    helpBtn.onClick = [this] { help.setVisible (true); help.toFront (true); };
    addChildComponent (help);
    help.onClose = [this] { help.setVisible (false); };
    addChildComponent (about);
    about.onClose = [this] { about.setVisible (false); };

    setSize (800, 570);
    startTimerHz (24);
}

void VocalRiderEditor::setupControl (Control& c, const juce::String& title, const juce::String& hintText,
                                     const juce::String& paramId, juce::Slider::SliderStyle style)
{
    const bool vertical = style == juce::Slider::LinearVertical;
    c.name.setText (title, juce::dontSendNotification);
    c.name.setFont (juce::Font (vertical ? 11.5f : 13.0f, juce::Font::bold));
    c.name.setColour (juce::Label::textColourId, kText);
    c.name.setJustificationType (juce::Justification::centred);
    c.hint.setText (hintText, juce::dontSendNotification);
    c.hint.setFont (juce::Font (vertical ? 10.5f : 12.0f));
    c.hint.setColour (juce::Label::textColourId, kDim);
    c.hint.setJustificationType (vertical ? juce::Justification::centred : juce::Justification::topLeft);
    c.slider.setSliderStyle (style);
    c.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, vertical ? 56 : 80, 20);
    c.slider.setColour (juce::Slider::thumbColourId, kAfter);
    c.slider.setColour (juce::Slider::trackColourId, kAfter);
    c.slider.setColour (juce::Slider::rotarySliderFillColourId, kAfter);
    c.slider.setColour (juce::Slider::rotarySliderOutlineColourId, kGrid);
    c.slider.setColour (juce::Slider::backgroundColourId, kGrid);
    c.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    c.slider.setColour (juce::Slider::textBoxBackgroundColourId, kPanel2);
    c.slider.setColour (juce::Slider::textBoxOutlineColourId, kGrid);
    addAndMakeVisible (c.name);
    addAndMakeVisible (c.hint);
    addAndMakeVisible (c.slider);
    c.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, c.slider);
}

void VocalRiderEditor::setupToggle (Toggle& t, const juce::String& title, const juce::String& hintText,
                                    const juce::String& paramId)
{
    t.button.setButtonText (title);
    t.button.setColour (juce::ToggleButton::textColourId, kText);
    t.button.setColour (juce::ToggleButton::tickColourId, kAfter);
    t.button.setColour (juce::ToggleButton::tickDisabledColourId, kFaint);
    t.hint.setText (hintText, juce::dontSendNotification);
    t.hint.setFont (juce::Font (11.5f));
    t.hint.setColour (juce::Label::textColourId, kDim);
    t.hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (t.button);
    addAndMakeVisible (t.hint);
    t.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, paramId, t.button);
}


// ------------------------------------------------------------------ help button + guide
void VocalRiderEditor::HelpButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    const auto b = getLocalBounds().toFloat().reduced (3.0f);
    const auto col = isDown ? kAfter : (isOver ? kText : kDim);
    g.setColour (col);
    g.drawEllipse (b.reduced (1.0f), 1.6f);
    g.setFont (juce::Font (b.getHeight() * 0.78f, juce::Font::bold));
    g.drawText ("?", b.toNearestInt(), juce::Justification::centred);
}

VocalRiderEditor::HelpOverlay::HelpOverlay()
{
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (10);
    addAndMakeVisible (view);
    closeBtn.setColour (juce::TextButton::buttonColourId, kAfter);
    closeBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff101010));
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);
}

void VocalRiderEditor::HelpOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! cardR().contains (e.getPosition()) && onClose) onClose();
}

void VocalRiderEditor::HelpOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xe6101010));
    const auto r = cardR();
    card (g, r);
    g.setColour (kText);
    g.setFont (juce::Font (18.0f, juce::Font::bold));
    g.drawText ("Help: what every control does", r.getX() + 18, r.getY() + 10, 400, 28, juce::Justification::centredLeft);
    g.setColour (kAfter);
    g.fillRect (r.getX() + 18, r.getY() + 42, r.getWidth() - 36, 1);
}

void VocalRiderEditor::HelpOverlay::resized()
{
    const auto r = cardR();
    closeBtn.setBounds (r.getRight() - 18 - 70, r.getY() + 11, 70, 26);
    view.setBounds (r.getX() + 4, r.getY() + 48, r.getWidth() - 8, r.getHeight() - 54);
    content.build (view.getWidth() - view.getScrollBarThickness());
}

void VocalRiderEditor::HelpOverlay::Content::build (int width)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::topLeft);
    const auto head  = juce::Font (15.5f, juce::Font::bold);
    const auto label = juce::Font (12.5f, juce::Font::bold);
    const auto body  = juce::Font (12.5f);

    auto section = [&] (const juce::String& title, juce::Colour c, const juce::String& what,
                        const juce::String& how, const juce::String& works)
    {
        s.append ("\n" + title + "\n", head, c);
        s.append ("What it is for:  ", label, kText);  s.append (what + "\n", body, kDim);
        s.append ("How to use it:  ", label, kText);   s.append (how + "\n", body, kDim);
        s.append ("How it works:  ", label, kText);    s.append (works + "\n", body, kDim);
    };

    s.append ("Vocalise-Rider listens to your vocal and moves the volume up and down for you, like a person riding a fader. "
              "It only changes the volume, never the tone, so the sound stays clean. Start with AUTO SET, then fine-tune.\n",
              body, kText);

    section ("The graph: Original and After riding", kAfter,
        "Shows how loud the vocal is over the last 10 seconds. Grey is the original. Green is what comes out after riding.",
        "Play the vocal. Then drag the lines and turn the knobs. The green trace changes right away, even while Logic is stopped (it uses the last 10 seconds you played). You want the green to be flatter than the grey.",
        "Each 10 ms the plugin measures the loudness and works out how much volume change the current settings would make. The green trace is the original plus that change.");

    section ("LOUD line (purple)", kLoud,
        "Sets how loud the loud parts are allowed to get.",
        "Drag the purple line down to pull loud parts down harder. Drag it up to leave them alone.",
        "Anything louder than this line is turned down until it sits on the line (at most 18 dB of cut).");

    section ("QUIET line (blue)", kQuiet,
        "Sets how quiet the quiet parts are allowed to get.",
        "Drag the blue line up to lift quiet parts more. Drag it down to leave them alone.",
        "Anything quieter than this line is turned up until it sits on the line (at most 12 dB of boost). Silence is never lifted, see SILENCE below.");

    section ("TARGET line (teal, dashed)", kTarget,
        "The loudness you are aiming for. The loud and quiet lines sit above and below it.",
        "Drag it up or down. Grabbing it turns AUTO TARGET off so it stays where you put it.",
        "The plugin pulls the vocal toward this level, as much as LEVELING allows. Moving the target moves both other lines with it.");

    section ("AUTO TARGET (switch)", kTarget,
        "Lets the plugin find the target by itself.",
        "Leave it on for a vocal with a steady overall level. Turn it off, or grab the teal line, when you want a fixed number.",
        "It follows the singer's average loudness slowly (about a 10 second memory), so it ignores short loud or quiet moments.");

    section ("AUTO SET (button)", kAfter,
        "Sets the target, both lines and the silence level for you in one click.",
        "Play at least 10 seconds of the vocal (a whole verse and chorus is better). Click AUTO SET. Then adjust by hand if you like.",
        "It builds a picture of every loudness it heard while you played. It puts the target at the typical singing level, the lines where the loud and quiet extremes are, and the silence level just above the noise between phrases. It then starts listening fresh.");

    section ("LEVELING (slider, 0 to 10)", kText,
        "How strongly the plugin evens out the vocal. 0 does nothing extra, 10 is fully working.",
        "Raise it for a more even vocal. Lower it to keep more of the natural ups and downs. Around 4 to 6 is a good start.",
        "At 0 only the LOUD and QUIET lines act, so everything between them is left alone. As you go up, the plugin also pulls the in-between parts toward the target. At 10 the vocal is pulled all the way to the target.");

    section ("SILENCE (slider)", kText,
        "Tells the plugin where singing stops and background noise or breath begins.",
        "Set it just above the quiet noise between phrases. AUTO SET picks a value for you. The dotted line on the graph shows it.",
        "Anything below this level counts as silence. The plugin does not try to lift it, which keeps the noise floor, breaths and room sound from being pumped up.");

    section ("SPEED (knob, 0 to 10)", kText,
        "How fast the plugin reacts. Low is slow and smooth. High is quick and tight.",
        "Slow settings sound natural and hide the riding. Fast settings catch short peaks but can sound more worked. The text under the knob shows the real times in milliseconds or seconds.",
        "One knob sets two times. Loud parts are pulled down at the fast time (about 160 ms at 0, down to 16 ms at 10). Quiet parts come back up six times slower, which is gentler on the ear.");

    section ("SMART SILENCE (switch)", kAfter,
        "Stops the plugin from leaving a boost on when the singer stops.",
        "Leave it on. Turn it off only if you hear the end of words being cut short.",
        "When the level falls below SILENCE, any boost slowly relaxes back to no change. The next phrase then starts from a clean, neutral level instead of a lifted one.");

    section ("PROTECT S SOUNDS (switch)", kAfter,
        "Keeps S, T and breath sounds from steering the ride.",
        "Leave it on for most vocals. Turn it off if you want every sound to be ridden.",
        "The plugin notices frames that are mostly high pitched hiss (above about 5 kHz). Those frames are held and do not move the ride, so a harsh S is not pulled down, and the next word is not lifted because of it.");

    section ("OUTPUT (slider)", kText,
        "The main volume of the plugin, from -24 to +12 dB.",
        "Use it to set the level that leaves the plugin. Watch the PEAK meter while you do.",
        "A plain volume after the ride. It changes smoothly, so moving it makes no clicks.");

    section ("PEAK meter", kWarn,
        "Shows how loud the plugin's output is, left and right, so you can see if it is peaking.",
        "Keep the peaks out of the red. The CLIP light stays on if the signal reached the top. Click the meter to reset it.",
        "It shows the highest level of each block after the OUTPUT volume, with a small marker that holds the recent peak for about 1.5 seconds.");

    section ("LIVE / WRITE / READ (mode buttons)", kTarget,
        "Lets you record the ride as Logic automation, then bounce it.",
        "LIVE: rides while playing, nothing is recorded. WRITE: set the Logic track to Latch or Touch, press play, and the ride is recorded to the \"Ride (dB)\" automation lane. READ: set the track to Read and the recorded ride is played back. You can then Command-select and edit it. To redo a ride, switch to WRITE and play over it.",
        "In WRITE the plugin moves the \"Ride (dB)\" parameter itself while the transport is running. In READ it ignores the audio and applies whatever that parameter says. The plugin reports 30 ms of delay, so keep Plug-in Delay Compensation on in Logic.");

    section ("ORIGINAL vs AFTER RIDING numbers", kText,
        "Tells you in numbers how much more even the vocal became.",
        "Look at the gap. A smaller gap after riding means a more even vocal.",
        "LOUD is the level the louder parts reach, QUIET is the level the quieter parts reach, and GAP is the distance between them. Silence is left out of these numbers.");

    s.append ("\nAutomation: every control above can also be automated in Logic from the track's automation menu.\n", body, kDim);
    s.append ("Tip: bounce in place when you are happy so the ride is baked in.\n", body, kDim);

    layout.createLayout (s, (float) std::max (100, width - 36));
    setSize (width, (int) std::ceil (layout.getHeight()) + 28);
}

// ------------------------------------------------------------------ gear + about
void VocalRiderEditor::GearButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    const auto b = getLocalBounds().toFloat().reduced (3.0f);
    const auto c = b.getCentre();
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    juce::Path cog;
    cog.setUsingNonZeroWinding (false);                       // so the centre hole is cut out
    cog.addStar (c, 8, r * 0.72f, r, 0.0f);
    cog.addEllipse (c.x - r * 0.32f, c.y - r * 0.32f, r * 0.64f, r * 0.64f);
    g.setColour (isDown ? kAfter : (isOver ? kText : kDim));
    g.fillPath (cog);
}

void VocalRiderEditor::AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xe6101010));
    auto card_r = juce::Rectangle<int> (0, 0, 480, 250).withCentre (getLocalBounds().getCentre());
    card (g, card_r);

    auto r = card_r.reduced (24);
    if (logo.isValid())
    {
        auto logoR = r.removeFromLeft (88).removeFromTop (88);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, logoR.toFloat(), juce::RectanglePlacement::centred);
        r.removeFromLeft (18);
    }
    g.setColour (kText);
    g.setFont (juce::Font (18.0f, juce::Font::bold));
    g.drawText ("VOCALISE-RIDER", r.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (kAfter);
    g.fillRect (r.removeFromTop (1));
    r.removeFromTop (14);

    g.setColour (kDim);
    g.setFont (juce::Font (13.0f));
    g.drawText (juce::String ("Version ") + JucePlugin_VersionString, r.removeFromTop (20), juce::Justification::centredLeft);
    r.removeFromTop (10);

    g.setColour (kText);
    g.setFont (juce::Font (16.0f, juce::Font::bold));
    g.drawText ("Made with JUCE and Claude", r.removeFromTop (26), juce::Justification::centredLeft);
    r.removeFromTop (6);

    g.setColour (kDim);
    g.setFont (juce::Font (12.5f));
    g.drawFittedText ("A Daemonj Multimedia plugin.\nAudio framework: JUCE (juce.com). Code written with Claude, an AI assistant by Anthropic.",
                      r.removeFromTop (70), juce::Justification::topLeft, 5);

    g.setColour (kFaint);
    g.setFont (juce::Font (12.0f));
    g.drawText ("Click anywhere to close", card_r.reduced (24).removeFromBottom (16), juce::Justification::centredLeft);
}

// ------------------------------------------------------------------ layout
void VocalRiderEditor::resized()
{
    about.setBounds (getLocalBounds());
    help.setBounds (getLocalBounds());
    gear.setBounds (getWidth() - 16 - 26, 10, 26, 26);
    helpBtn.setBounds (getWidth() - 16 - 26 - 6 - 26, 10, 26, 26);

    const int m = 16, sideW = 232, gap = 12;
    const int leftW = getWidth() - 2 * m - sideW - gap;

    graphArea = { m, 48, leftW, 250 };
    statsArea = { m, graphArea.getBottom() + 8, leftW, 78 };
    sideArea  = { m + leftW + gap, 48, sideW, statsArea.getBottom() - 48 };
    speedArea = { m, statsArea.getBottom() + 12, getWidth() - 2 * m, getHeight() - m - (statsArea.getBottom() + 12) };

    // side column: three vertical sliders + peak meter
    auto col = sideArea.reduced (6, 8);
    meterArea = col.removeFromRight (44);
    const int cw = col.getWidth() / 3;
    auto place = [&] (Control& c, juce::Rectangle<int> r)
    {
        c.name.setBounds (r.removeFromTop (18));
        c.hint.setBounds (r.removeFromBottom (30));
        c.slider.setBounds (r);
    };
    place (cLevel, col.removeFromLeft (cw));
    place (cSilence, col.removeFromLeft (cw));
    place (cOutput, col);

    // bottom card: mode switch sits in the title row, right of the pill
    {
        int x = speedArea.getX() + 16 + 104 + 16;
        for (auto& b : modeBtn) { b.setBounds (x, speedArea.getY() + 9, 62, 24); x += 66; }
        modeHint.setBounds (x + 8, speedArea.getY() + 9, speedArea.getRight() - x - 24, 24);
    }
    auto s = speedArea.reduced (16, 12);
    s.removeFromTop (22);                                         // card title is drawn in paint()

    auto a = s.removeFromLeft (250);                              // SPEED knob + live description
    {
        auto k = a.removeFromLeft (104);
        cSpeed.name.setBounds (k.removeFromTop (16));
        cSpeed.slider.setBounds (k);
        a.removeFromLeft (6);
        cSpeed.hint.setBounds (a.removeFromTop (80));
    }
    s.removeFromLeft (10);

    auto b = s.removeFromLeft (220);                              // two switches with explanations
    tSmart.button.setBounds (b.removeFromTop (22));
    tSmart.hint.setBounds (b.removeFromTop (38));
    b.removeFromTop (4);
    tProtect.button.setBounds (b.removeFromTop (22));
    tProtect.hint.setBounds (b.removeFromTop (38));
    s.removeFromLeft (10);

    auto c = s;                                                   // AUTO TARGET + AUTO SET
    auto row = c.removeFromTop (28);
    tAuto.button.setBounds (row.removeFromLeft (130).withTrimmedTop (3));
    autoSetButton.setBounds (row.withTrimmedLeft (4));
    c.removeFromTop (4);
    tAuto.hint.setBounds (c);
}

// ------------------------------------------------------------------ graph helpers
float VocalRiderEditor::dbToY (float db) const
{
    const float c = juce::jlimit (kDbBot, kDbTop, db);
    return (float) graphArea.getY() + 6.0f + (kDbTop - c) / (kDbTop - kDbBot) * (float) (graphArea.getHeight() - 12);
}

float VocalRiderEditor::yToDb (float y) const
{
    const float f = (y - (float) graphArea.getY() - 6.0f) / (float) (graphArea.getHeight() - 12);
    return juce::jlimit (kDbBot, kDbTop, kDbTop - f * (kDbTop - kDbBot));
}

VocalRiderEditor::Line VocalRiderEditor::lineAt (juce::Point<int> pt) const
{
    if (! graphArea.contains (pt)) return Line::none;
    const float tgt = currentTarget();
    const float yT = dbToY (tgt);
    const float yL = dbToY (tgt + param ("loudAbove"));
    const float yQ = dbToY (tgt - param ("quietBelow"));
    const float y = (float) pt.y;
    const float dT = std::abs (y - yT), dL = std::abs (y - yL), dQ = std::abs (y - yQ);
    const float best = std::min ({ dT, dL, dQ });
    if (best > 8.0f) return Line::none;
    if (dL == best && dQ == best) return y < yT ? Line::loud : Line::quiet;   // both at the target line
    if (dL == best) return Line::loud;
    if (dQ == best) return Line::quiet;
    return Line::target;
}

void VocalRiderEditor::setParam (const char* id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void VocalRiderEditor::commitParam (const char* id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void VocalRiderEditor::mouseMove (const juce::MouseEvent& e)
{
    hover = lineAt (e.getPosition());
    setMouseCursor (hover == Line::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
}

void VocalRiderEditor::mouseDown (const juce::MouseEvent& e)
{
    if (meterArea.contains (e.getPosition()))                  // click the meter to clear CLIP and the hold marker
    {
        clipped = false;
        peakHold[0] = peakHold[1] = -100.0f;
        return;
    }
    dragging = lineAt (e.getPosition());
    if (dragging == Line::none) return;
    const char* id = dragging == Line::loud ? "loudAbove" : dragging == Line::quiet ? "quietBelow" : "target";
    if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture();
    if (dragging == Line::target)
    {
        // grabbing the target line means "I'll set it myself": start from where it is now, then switch Auto off
        const float startAt = currentTarget();
        setParam ("target", startAt);
        commitParam ("autoTarget", 0.0f);
    }
}

void VocalRiderEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Line::none) return;
    const float db = yToDb ((float) e.y);
    if (dragging == Line::target)      setParam ("target", db);
    else if (dragging == Line::loud)   setParam ("loudAbove", juce::jlimit (0.0f, 24.0f, db - currentTarget()));
    else                               setParam ("quietBelow", juce::jlimit (0.0f, 24.0f, currentTarget() - db));
}

void VocalRiderEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Line::none) return;
    const char* id = dragging == Line::loud ? "loudAbove" : dragging == Line::quiet ? "quietBelow" : "target";
    if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture();
    dragging = Line::none;
}

// ------------------------------------------------------------------ AUTO SET
void VocalRiderEditor::runAutoSet()
{
    uint32_t h[RiderCore::kHistBins];
    proc.getCore().readLevelHistogram (h);
    uint64_t total = 0;
    for (auto v : h) total += v;
    if (total < 500)
    {
        autoStatus = "Play at least 5 seconds of the vocal first, then click AUTO SET.";
        return;
    }

    auto binAt = [&] (uint64_t countTarget, int fromBin)          // first bin where the running count reaches countTarget
    {
        uint64_t run = 0;
        for (int i = fromBin; i < RiderCore::kHistBins; ++i) { run += h[i]; if (run >= countTarget) return i; }
        return RiderCore::kHistBins - 1;
    };

    // Smart silence: the room-noise level is near the bottom of everything measured; put the threshold just above it.
    const float noise = RiderCore::histBinToDb (binAt ((uint64_t) (0.05 * (double) total), 0));
    const float gate = juce::jlimit (-70.0f, -40.0f, noise + 6.0f);

    int firstVoiced = 0;
    while (firstVoiced < RiderCore::kHistBins - 1 && RiderCore::histBinToDb (firstVoiced) <= gate) ++firstVoiced;
    uint64_t voiced = 0;
    for (int i = firstVoiced; i < RiderCore::kHistBins; ++i) voiced += h[i];
    if (voiced < 300)
    {
        autoStatus = "Not enough vocal above the silence level yet. Play more of the vocal and try again.";
        return;
    }

    const float p10 = RiderCore::histBinToDb (binAt ((uint64_t) (0.10 * (double) voiced), firstVoiced));
    const float p50 = RiderCore::histBinToDb (binAt ((uint64_t) (0.50 * (double) voiced), firstVoiced));
    const float p90 = RiderCore::histBinToDb (binAt ((uint64_t) (0.90 * (double) voiced), firstVoiced));

    commitParam ("autoTarget", 0.0f);
    commitParam ("target", p50);
    commitParam ("loudAbove", juce::jlimit (1.5f, 12.0f, 0.5f * (p90 - p50)));
    commitParam ("quietBelow", juce::jlimit (1.5f, 12.0f, 0.5f * (p50 - p10)));
    commitParam ("gate", gate);

    autoStatus = "Set from " + juce::String ((double) total / 100.0, 0) + " s of vocal: target "
               + juce::String (p50, 1) + " dB, silence " + juce::String (gate, 0) + " dB.";
    proc.getCoreForEditor().clearLevelHistogram();                // next AUTO SET starts fresh
}

// ------------------------------------------------------------------ timer: copy data, preview the ride, meters
RiderParams VocalRiderEditor::currentParams() const
{
    RiderParams p;
    p.autoTarget   = param ("autoTarget") > 0.5f;
    p.targetDb     = param ("target");
    p.loudAbove    = param ("loudAbove");
    p.quietBelow   = param ("quietBelow");
    p.leveling     = param ("leveling");
    p.gateDb       = param ("gate");
    p.speed        = param ("speed");
    p.smartSilence = param ("smartSilence") > 0.5f;
    p.protectS     = param ("protectS") > 0.5f;
    return p;
}

void VocalRiderEditor::timerCallback()
{
    proc.getCore().readHistory (before, level, sib);
    shownTarget = proc.getCore().getShownTargetDb();

    // ---- AFTER RIDING preview: the same decisions the plugin makes, re-run on the stored 10 s of vocal
    //      with your CURRENT settings, so moving a line or knob reshapes the green trace right away. ----
    const auto p = currentParams();
    RiderCore::simulate (level, sib, N, p, currentTarget(), gainDb);
    for (int i = 0; i < N; ++i)
    {
        if (before[i] < -99.0f) { after[i] = -100.0f; continue; }
        const int j = std::min (i + RiderCore::kLookaheadFrames, N - 1);   // the ride leads the audio by the lookahead
        after[i] = before[i] + gainDb[j];
    }

    // ---- numbers: loud (90th percentile) and quiet (10th percentile) of the singing, original vs after ----
    std::vector<float> b, a;
    for (int i = 0; i < N; ++i)
        if (level[i] > p.gateDb && before[i] > -99.0f) { b.push_back (before[i]); a.push_back (after[i]); }
    haveStats = b.size() >= 30;
    if (haveStats)
    {
        statBefore[0] = percentile (b, 0.9f);  statBefore[1] = percentile (b, 0.1f);  statBefore[2] = statBefore[0] - statBefore[1];
        statAfter[0]  = percentile (a, 0.9f);  statAfter[1]  = percentile (a, 0.1f);  statAfter[2]  = statAfter[0]  - statAfter[1];
    }

    // ---- hints that depend on the settings ----
    float atk, rel;
    speedToMs (p.speed, atk, rel);
    auto ms = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " s" : juce::String ((int) std::lround (v)) + " ms"; };
    cSpeed.hint.setText ("How fast it reacts. A loud part is pulled down in about " + ms (atk)
                         + ", and quiet parts come back up in about " + ms (rel) + ".", juce::dontSendNotification);
    tAuto.hint.setText (! autoStatus.isEmpty() ? autoStatus
                        : (p.autoTarget ? "Following the singer's average loudness (" + juce::String (shownTarget, 1)
                                          + " dB). AUTO SET: play the vocal 10+ seconds, then click it to set the target, lines and silence for you."
                                        : "You set the TARGET line. AUTO SET: play the vocal 10+ seconds, then click it to set the target, lines and silence for you."),
                        juce::dontSendNotification);

    // ---- ride mode buttons ----
    {
        const int mode = juce::jlimit (0, 2, (int) std::lround (param ("mode")));
        for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState (i == mode, juce::dontSendNotification),
                                    modeBtn[i].setColour (juce::TextButton::buttonColourId, i == mode ? kAfter : kPanel2);
        modeHint.setText (mode == 0 ? "Rides live. Nothing is recorded."
                        : mode == 1 ? "Press play: the ride is written to automation (Logic track: Latch or Touch)."
                                    : "Playing back the recorded ride (Logic track: Read). Nothing is analysed.",
                          juce::dontSendNotification);
    }

    // ---- peak meter ----
    for (int c = 0; c < 2; ++c)
    {
        const float raw = proc.takePeak (c);
        const float db = toDb (raw);
        if (raw >= 1.0f) clipped = true;
        peakShown[c] = std::max (db, peakShown[c] - 1.3f);              // falls about 30 dB per second
        if (db >= peakHold[c]) { peakHold[c] = db; holdAge[c] = 0; }
        else if (++holdAge[c] > 36) peakHold[c] -= 0.8f;                // hold for 1.5 s, then drift down
    }
    repaint();
}

// ------------------------------------------------------------------ paint
void VocalRiderEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // top bar, like the Vocalise app: slightly lighter strip with a thin line underneath
    g.setColour (juce::Colour (0xff1e1e1e));
    g.fillRect (0, 0, getWidth(), 44);
    g.setColour (kGrid);
    g.fillRect (0, 43, getWidth(), 1);
    if (logo.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, juce::Rectangle<float> (14.0f, 8.0f, 28.0f, 28.0f), juce::RectanglePlacement::centred);
    }
    g.setColour (kText);
    g.setFont (juce::Font (17.0f, juce::Font::bold));
    g.drawText ("Vocalise-Rider", 50, 8, 150, 28, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::Font (11.0f));
    g.drawText (juce::String ("v") + JucePlugin_VersionString, 162, 14, 60, 16, juce::Justification::centredLeft);

    // legend (original / after)
    g.setFont (juce::Font (12.0f));
    int lx = getWidth() - 16 - 36 - 32;
    auto legend = [&] (const juce::String& t, juce::Colour c)
    {
        const int tw = 6 * t.length() + 14;
        lx -= tw;
        g.setColour (c);
        g.fillRect (lx, 21, 10, 3);
        g.setColour (kDim);
        g.drawText (t, lx + 14, 13, tw, 18, juce::Justification::centredLeft);
        lx -= 12;
    };
    legend ("After riding", kAfter);
    legend ("Original", kBefore);

    const float x0 = (float) graphArea.getX() + (float) kPlotLeft;
    const float x1 = (float) graphArea.getRight() - (float) kPlotRight;

    // ---------- level graph ----------
    card (g, graphArea);
    g.setFont (juce::Font (11.0f));
    for (int d = -10; d >= -50; d -= 10)
    {
        g.setColour (kGrid);
        g.drawHorizontalLine ((int) dbToY ((float) d), x0, x1);
        g.setColour (kDim);
        g.drawText (juce::String (d), graphArea.getX() + 4, (int) dbToY ((float) d) - 7, 28, 14, juce::Justification::centredRight);
    }

    const float tgt = currentTarget();
    const float loudOff = param ("loudAbove"), quietOff = param ("quietBelow");
    const float yT = dbToY (tgt), yL = dbToY (tgt + loudOff), yQ = dbToY (tgt - quietOff);

    g.setColour (kAfter.withAlpha (0.07f));                   // the zone between the two lines
    g.fillRect (juce::Rectangle<float> (x0, yL, x1 - x0, yQ - yL));

    auto drawTrace = [&] (const float* data, juce::Colour c, float thick)
    {
        juce::Path path;
        bool started = false;
        for (int i = 4; i < N; i += 2)
        {
            if (data[i] < -99.0f || data[i - 3] < -99.0f) { started = false; continue; }
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += data[i - k];
            s *= 0.25f;                              // light smoothing so it is readable
            const float x = x0 + (x1 - x0) * (float) i / (float) (N - 1);
            const float y = dbToY (s);
            if (! started) { path.startNewSubPath (x, y); started = true; }
            else path.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (thick));
    };
    drawTrace (before, kBefore, 1.3f);
    drawTrace (after, kAfter, 1.9f);

    // the silence threshold (set with the SILENCE slider)
    {
        const float yG = dbToY (param ("gate"));
        const float dots[] = { 2.0f, 4.0f };
        g.setColour (kFaint);
        g.drawDashedLine (juce::Line<float> (x0, yG, x1, yG), dots, 2, 1.0f);
        g.setFont (juce::Font (10.5f));
        g.drawText ("silence", (int) x1 - 52, (int) yG - 13, 50, 12, juce::Justification::centredRight);
    }

    // the three draggable lines
    auto drawLine = [&] (float y, juce::Colour c, const juce::String& tag, bool active, bool dashed)
    {
        g.setColour (c.withAlpha (active ? 1.0f : 0.85f));
        if (dashed)
        {
            const float dashes[] = { 6.0f, 4.0f };
            g.drawDashedLine (juce::Line<float> (x0, y, x1, y), dashes, 2, active ? 2.5f : 1.6f);
        }
        else g.fillRect (juce::Rectangle<float> (x0, y - (active ? 1.25f : 0.8f), x1 - x0, active ? 2.5f : 1.6f));

        const juce::Rectangle<float> tagR (x0 + 4.0f, y - 9.0f, 118.0f, 18.0f);   // handle tag at the left end
        g.setColour (c);
        g.fillRoundedRectangle (tagR, 9.0f);
        g.setColour (juce::Colour (0xff181818));
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.drawText (tag, tagR.toNearestInt(), juce::Justification::centred);
    };
    auto fmt = [] (float v) { return juce::String (v, 1); };
    drawLine (yL, kLoud, juce::String ("LOUD  +") + fmt (loudOff) + " dB", hover == Line::loud || dragging == Line::loud, false);
    drawLine (yQ, kQuiet, juce::String ("QUIET  -") + fmt (quietOff) + " dB", hover == Line::quiet || dragging == Line::quiet, false);
    drawLine (yT, kTarget, juce::String ("TARGET  ") + fmt (tgt) + " dB", hover == Line::target || dragging == Line::target, true);

    // ---------- original vs after numbers ----------
    card (g, statsArea);
    {
        const int x = statsArea.getX(), y = statsArea.getY();
        g.setFont (juce::Font (11.0f, juce::Font::bold));
        g.setColour (kDim);
        g.drawText ("ORIGINAL", x + 150, y + 6, 80, 14, juce::Justification::centredRight);
        g.setColour (kAfter);
        g.drawText ("AFTER RIDING", x + 236, y + 6, 100, 14, juce::Justification::centredRight);

        const char* names[3] = { "Loud parts", "Quiet parts", "Gap (loud - quiet)" };
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        for (int r = 0; r < 3; ++r)
        {
            const int ry = y + 22 + r * 17;
            g.setColour (kText);
            g.drawText (names[r], x + 14, ry, 140, 16, juce::Justification::centredLeft);
            g.setColour (kDim);
            g.drawText (haveStats ? fmt (statBefore[r]) + " dB" : "-", x + 150, ry, 80, 16, juce::Justification::centredRight);
            g.setColour (kAfter);
            g.drawText (haveStats ? fmt (statAfter[r]) + " dB" : "-", x + 236, ry, 100, 16, juce::Justification::centredRight);
        }
        g.setColour (kDim);
        g.setFont (juce::Font (11.0f));
        g.drawFittedText ("The gap is how far apart the loud and quiet parts are. A smaller gap means a more even vocal.",
                          juce::Rectangle<int> (x + 350, y + 8, statsArea.getWidth() - 362, 62), juce::Justification::topLeft, 4);
    }

    // ---------- side column ----------
    card (g, sideArea);

    // peak meter (after the OUTPUT volume)
    {
        auto m = meterArea;
        g.setColour (kText);
        g.setFont (juce::Font (11.5f, juce::Font::bold));
        g.drawText ("PEAK", m.removeFromTop (18), juce::Justification::centred);
        auto readout = m.removeFromBottom (30);
        const auto bars = m.reduced (2, 4);
        const float top = (float) bars.getY(), bot = (float) bars.getBottom();
        auto yOf = [&] (float db) { return bot - (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f * (bot - top); };

        juce::ColourGradient grad (kClip, 0.0f, top, kAfter, 0.0f, bot, false);
        grad.addColour (0.03, kClip);
        grad.addColour (0.08, kWarn);                         // about -5 dB
        grad.addColour (0.20, kAfter);                        // about -12 dB
        for (int c = 0; c < 2; ++c)
        {
            const juce::Rectangle<float> bar ((float) bars.getX() + (float) c * 20.0f, top, 15.0f, bot - top);
            g.setColour (kPanel2);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> (bar.getX(), yOf (peakShown[c]), bar.getWidth(), bot - yOf (peakShown[c])));
            if (peakHold[c] > -59.0f)
            {
                g.setColour (peakHold[c] > -1.0f ? kClip : kText);
                g.fillRect (bar.getX(), yOf (peakHold[c]) - 1.0f, bar.getWidth(), 2.0f);
            }
        }
        g.setColour (kFaint);                                 // 0 dB mark
        g.drawHorizontalLine ((int) yOf (0.0f), (float) bars.getX() - 2.0f, (float) bars.getRight() + 2.0f);

        const float hold = std::max (peakHold[0], peakHold[1]);
        g.setFont (juce::Font (12.0f, juce::Font::bold));
        g.setColour (clipped ? kClip : kText);
        g.drawText (clipped ? "CLIP" : (hold > -99.0f ? juce::String (hold, 1) : juce::String ("-inf")),
                    readout.removeFromTop (16), juce::Justification::centred);
        g.setColour (kFaint);
        g.setFont (juce::Font (10.0f));
        g.drawText ("click to reset", readout, juce::Justification::centred);
    }

    // ---------- bottom card ----------
    card (g, speedArea);
    {
        const juce::Rectangle<float> pill ((float) speedArea.getX() + 16.0f, (float) speedArea.getY() + 10.0f, 104.0f, 22.0f);
        g.setColour (juce::Colour (0xff263238));              // --header-badge-bg
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (juce::Colour (0xff00838f));              // --header-badge-border
        g.drawRoundedRectangle (pill.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (kTarget);                                // --header-badge-txt
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText ("How it rides", pill.toNearestInt(), juce::Justification::centred);
    }
}
