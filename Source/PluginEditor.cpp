#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
    // Palette taken from the Vocalise app's dark theme
    const juce::Colour kBg      (0xff181818);   // --bg-primary
    const juce::Colour kPanel   (0xff212121);   // --bg-secondary
    const juce::Colour kPanel2  (0xff272727);   // --bg-tertiary
    const juce::Colour kGrid    (0xff313131);   // --border-color
    const juce::Colour kBefore  (0xff8a8a8a);   // original vocal
    const juce::Colour kAfter   (0xff4caf50);   // --accent green: after riding
    const juce::Colour kLoud    (0xffce93d8);   // purple: TAME PEAKS
    const juce::Colour kQuiet   (0xff90caf9);   // blue:   LOWER LEVELS
    const juce::Colour kTarget  (0xff80deea);   // teal:   TARGET
    const juce::Colour kText    (0xffe0e0e0);   // --text-main
    const juce::Colour kDim     (0xff888888);   // --text-muted
    const juce::Colour kFaint   (0xff555555);   // --text-faint
    const juce::Colour kBody    (0xffbdbdbd);   // body text on dark cards
    const juce::Colour kWarn    (0xffffb454);   // meter: getting hot
    const juce::Colour kClip    (0xffe53935);   // meter: clipping / "click to clear"
    const juce::Colour kInk     (0xff101010);

    constexpr float kDbTop = 0.0f, kDbBot = -60.0f;

    // The Vocalise app uses Futura. Macs have it built in; other systems fall back to the default sans-serif.
    juce::Font uiFont (float size, bool bold = false)
    {
        static const juce::String face = []
        {
            const auto names = juce::Font::findAllTypefaceNames();
            for (const char* n : { "Futura", "Futura PT", "Jost", "Century Gothic" })
                if (names.contains (n)) return juce::String (n);
            return juce::Font::getDefaultSansSerifFontName();
        }();
        static const bool hasMedium = juce::Font::findAllTypefaceStyles (face).contains ("Medium");
        size *= 1.14f;                                                    // everything a little larger than the first version
        if (bold)           return juce::Font (face, size, juce::Font::bold);
        if (hasMedium)      return juce::Font (face, "Medium", size);     // Futura Book is thin; Medium reads better on dark
        return juce::Font (face, size, juce::Font::plain);
    }

    void card (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour border = kGrid, float alpha = 1.0f)
    {
        g.setColour (kPanel.withAlpha (alpha));
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        g.setColour (border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    void badge (juce::Graphics& g, juce::Point<int> centre, int n, juce::Colour c)
    {
        const auto r = juce::Rectangle<float> (15.0f, 15.0f).withCentre (centre.toFloat());
        g.setColour (c);
        g.fillEllipse (r);
        g.setColour (kInk);
        g.setFont (uiFont (10.0f, true));
        g.drawText (juce::String (n), r.toNearestInt(), juce::Justification::centred, false);
    }

    float percentile (std::vector<float>& v, float q)
    {
        std::sort (v.begin(), v.end());
        const auto idx = (size_t) juce::jlimit (0.0f, (float) (v.size() - 1), q * (float) (v.size() - 1));
        return v[idx];
    }

    float toDb (float lin) { return lin > 1.0e-5f ? 20.0f * std::log10 (lin) : -100.0f; }
    juce::String signedDb (float v, int dp = 1) { return (v >= 0.0f ? "+" : "-") + juce::String (std::abs (v), dp) + " dB"; }
}

//==============================================================================  look and feel
VocalRiderEditor::RiderLook::RiderLook()
{
    setColour (juce::Label::textColourId, kText);
    setColour (juce::Label::textWhenEditingColourId, kText);
    setColour (juce::Label::backgroundWhenEditingColourId, kBg);
    setColour (juce::Label::outlineWhenEditingColourId, kAfter);
    setColour (juce::TextEditor::textColourId, kText);
    setColour (juce::TextEditor::backgroundColourId, kBg);
    setColour (juce::TextEditor::highlightColourId, kAfter.withAlpha (0.4f));
    setColour (juce::TextEditor::highlightedTextColourId, kText);
    setColour (juce::TextEditor::outlineColourId, kGrid);
    setColour (juce::TextEditor::focusedOutlineColourId, kAfter);
    setColour (juce::CaretComponent::caretColourId, kAfter);
    setColour (juce::TextButton::buttonColourId, kPanel2);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::ComboBox::backgroundColourId, kBg);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kGrid);
    setColour (juce::ComboBox::arrowColourId, kAfter);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAfter);
    setColour (juce::PopupMenu::highlightedTextColourId, kInk);
    setColour (juce::TooltipWindow::backgroundColourId, kPanel2);
    setColour (juce::TooltipWindow::textColourId, kText);
    setColour (juce::TooltipWindow::outlineColourId, kGrid);
    setColour (juce::Slider::thumbColourId, kAfter);
    setColour (juce::Slider::trackColourId, kAfter);
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff3a3a3a));
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxBackgroundColourId, kBg);
    setColour (juce::Slider::textBoxOutlineColourId, kGrid);
    setColour (juce::ScrollBar::thumbColourId, kFaint);
}

juce::Font VocalRiderEditor::RiderLook::getLabelFont (juce::Label& l)
{
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr) return uiFont (12.5f, true);
    return l.getFont();
}
juce::Font VocalRiderEditor::RiderLook::getComboBoxFont (juce::ComboBox&) { return uiFont (13.0f, true); }
juce::Font VocalRiderEditor::RiderLook::getPopupMenuFont() { return uiFont (13.0f); }
juce::Font VocalRiderEditor::RiderLook::getTextButtonFont (juce::TextButton&, int) { return uiFont (12.0f, true); }

void VocalRiderEditor::RiderLook::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr)          // the value box under a knob / fader
    {
        const auto r = l.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colour (0xff1c1c1c));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (l.isBeingEdited() ? kAfter : juce::Colour (0xff6a6a6a));
        g.drawRoundedRectangle (r, 5.0f, 1.2f);
        if (! l.isBeingEdited())
        {
            g.setColour (kText);
            g.setFont (getLabelFont (l));
            g.drawText (l.getText(), l.getLocalBounds(), juce::Justification::centred, false);
        }
        return;
    }
    juce::LookAndFeel_V4::drawLabel (g, l);
}

void VocalRiderEditor::RiderLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                                    float startAngle, float endAngle, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto c = b.getCentre();
    const float thick = 9.0f;
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 3.0f - thick * 0.5f;
    const float ang = startAngle + pos * (endAngle - startAngle);
    const juce::PathStrokeType stroke (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.strokePath (track, stroke);

    juce::Path value;
    value.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, ang, true);
    g.setColour (s.isEnabled() ? kAfter : kFaint);
    g.strokePath (value, stroke);

    const float ri = r - thick * 0.5f - 3.0f;                                    // dark centre with the faint caption
    g.setColour (juce::Colour (0xff1c1c1c));
    g.fillEllipse (c.x - ri, c.y - ri, ri * 2.0f, ri * 2.0f);
    g.setColour (kFaint);
    g.setFont (uiFont (7.5f));
    g.drawFittedText (s.getProperties().getWithDefault ("caption", "").toString(),
                      juce::Rectangle<float> (c.x - ri * 0.8f, c.y - ri * 0.5f, ri * 1.6f, ri).toNearestInt(),
                      juce::Justification::centred, 3, 1.0f);
}

void VocalRiderEditor::RiderLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos,
                                                    float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }
    const float cx = (float) x + (float) w * 0.5f;
    const float top = (float) y + 6.0f, bot = (float) y + (float) h - 6.0f;
    pos = juce::jlimit (top, bot, pos);

    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (cx - 3.0f, top, 6.0f, bot - top, 3.0f);
    g.setColour (kAfter);
    g.fillRoundedRectangle (cx - 3.0f, pos, 6.0f, bot - pos, 3.0f);

    if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)                           // unity (0 dB) marker
    {
        const float y0 = s.getPositionOfValue (0.0);
        g.setColour (juce::Colour (0xff6a6a6a));
        g.fillRect (cx - 24.0f, y0 - 0.5f, 48.0f, 1.0f);
    }
    g.setColour (kAfter);
    g.fillEllipse (cx - 8.0f, pos - 8.0f, 16.0f, 16.0f);
    g.setColour (juce::Colour (0xff2e7d32));
    g.drawEllipse (cx - 7.5f, pos - 7.5f, 15.0f, 15.0f, 1.0f);
}

//==============================================================================  small widgets
void VocalRiderEditor::IconButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto col = isDown ? kAfter : (isOver ? juce::Colour (0xffc4c4c4) : juce::Colour (0xff8a8a8a));
    g.setColour (col);
    if (kind == cog)
    {
        const auto c = b.getCentre();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        juce::Path p;
        p.setUsingNonZeroWinding (false);                                        // cuts the centre hole
        p.addStar (c, 8, r * 0.74f, r, 0.0f);
        p.addEllipse (c.x - r * 0.34f, c.y - r * 0.34f, r * 0.68f, r * 0.68f);
        g.fillPath (p);
        return;
    }
    g.fillEllipse (b);
    g.setColour (kBg);
    g.setFont (uiFont (b.getHeight() * 0.78f, true));
    g.drawText (kind == help ? "?" : "i", b.toNearestInt(), juce::Justification::centred, false);
}

void VocalRiderEditor::ModeButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::Colour fill, border, text;
    if (active) { fill = kPanel.interpolatedWith (accent, 0.14f); border = accent; text = accent; }
    else        { fill = juce::Colour (0xff2b2b2b); border = juce::Colour (0xff383838); text = juce::Colour (0xff7d7d7d); }
    if (isOver && ! active) { fill = fill.brighter (0.08f); text = juce::Colour (0xffb0b0b0); }
    if (isDown) fill = fill.brighter (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (border);
    g.drawRoundedRectangle (r, 5.0f, active ? 2.0f : 1.2f);
    g.setColour (text);
    g.setFont (uiFont (12.0f, true));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

void VocalRiderEditor::PowerToggle::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    const bool on = getToggleState();
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::Colour fill = on ? kPanel.interpolatedWith (kAfter, 0.14f) : juce::Colour (0xff2b2b2b);
    if (isOver) fill = fill.brighter (0.06f);
    if (isDown) fill = fill.brighter (0.08f);
    const auto col = on ? kAfter : juce::Colour (0xff8a8a8a);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? kAfter : juce::Colour (0xff383838));
    g.drawRoundedRectangle (r, 5.0f, on ? 2.0f : 1.2f);

    const float cy = r.getCentreY(), px = r.getX() + 22.0f;                     // power symbol
    juce::Path ring;
    ring.addCentredArc (px, cy + 1.0f, 6.5f, 6.5f, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
    g.setColour (col);
    g.strokePath (ring, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (px, cy - 8.0f, px, cy, 1.8f);
    g.setColour (col.withAlpha (0.55f));
    g.fillRect (r.getX() + 42.0f, cy - 8.0f, 1.2f, 16.0f);
    g.setColour (col);
    g.setFont (uiFont (13.0f, true));
    g.drawText (on ? "ON" : "OFF", juce::Rectangle<int> ((int) r.getX() + 44, 0, (int) r.getWidth() - 44, getHeight()),
                juce::Justification::centred, false);
}

void VocalRiderEditor::StepButton::paintButton (juce::Graphics& g, bool isOver, bool isDown)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    g.setColour (isDown ? kAfter.brighter (0.4f) : (isOver ? kAfter.brighter (0.2f) : kAfter));
    g.fillRoundedRectangle (c.x - 5.5f, c.y - 1.2f, 11.0f, 2.4f, 1.2f);
    if (plus) g.fillRoundedRectangle (c.x - 1.2f, c.y - 5.5f, 2.4f, 11.0f, 1.2f);
}

//==============================================================================  help overlay ("?")
VocalRiderEditor::HelpOverlay::HelpOverlay()
{
    for (auto* b : { &backBtn, &nextBtn, &closeBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanel2);
        b->setColour (juce::TextButton::textColourOffId, kText);
        addAndMakeVisible (*b);
    }
    nextBtn.setColour (juce::TextButton::buttonColourId, kAfter);
    nextBtn.setColour (juce::TextButton::textColourOffId, kInk);
    backBtn.onClick  = [this] { setPage (page - 1); };
    nextBtn.onClick  = [this] { setPage (page + 1); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
}

void VocalRiderEditor::HelpOverlay::setPage (int p)
{
    page = juce::jlimit (0, 2, p);
    backBtn.setVisible (page > 0);
    nextBtn.setVisible (page < 2);
    repaint();
}

void VocalRiderEditor::HelpOverlay::resized()
{
    backBtn.setBounds (200, 9, 66, 24);
    nextBtn.setBounds (272, 9, 66, 24);
    closeBtn.setBounds (344, 9, 66, 24);
}

void VocalRiderEditor::HelpOverlay::mouseDown (const juce::MouseEvent&) { if (onClose) onClose(); }

void VocalRiderEditor::HelpOverlay::layoutBoxes (const juce::Array<Callout>& list, juce::Array<juce::Rectangle<int>>& out,
                                                 float& bodySize) const
{
    const juce::Rectangle<int> region = page < 2 ? juce::Rectangle<int> (14, 356, 705, 160)
                                                 : juce::Rectangle<int> (14, 52, 705, 300);
    const int cols = page == 0 ? 2 : 3, gap = 10, colW = (region.getWidth() - gap * (cols - 1)) / cols;
    // Fonts differ between computers (Futura is wider than the fallback), so shrink the text until every box fits.
    for (bodySize = 10.5f; bodySize >= 8.0f; bodySize -= 0.5f)
    {
        out.clearQuick();
        int yCursor[3] = { region.getY(), region.getY(), region.getY() };   // one per column
        bool fits = true;
        for (auto& c : list)
        {
            juce::AttributedString s;
            s.append (c.body, uiFont (bodySize), kBody);
            juce::TextLayout tl;
            tl.createLayout (s, (float) (colW - 20));
            const int h = 8 + 18 + (int) std::ceil (tl.getHeight()) + 8;
            const int col = juce::jlimit (0, cols - 1, c.col);
            out.add ({ region.getX() + col * (colW + gap), yCursor[col], colW, h });
            yCursor[col] += h + 6;
            if (yCursor[col] - 6 > region.getBottom()) fits = false;
        }
        if (fits) break;
    }
}

void VocalRiderEditor::HelpOverlay::paint (juce::Graphics& g)
{
    juce::Array<Callout> list;
    if (getCallouts) list = getCallouts (page);

    {   // dim everything except the controls this page explains
        juce::Graphics::ScopedSaveState ss (g);
        for (auto& c : list)
            for (auto& t : c.targets)
                g.excludeClipRegion (t.expanded (3));
        g.fillAll (juce::Colour (0xe0101010));
    }
    for (auto& c : list)
        for (auto& t : c.targets)
        {
            g.setColour (c.colour);
            g.drawRoundedRectangle (t.expanded (3).toFloat(), 5.0f, 1.4f);
        }

    juce::Array<juce::Rectangle<int>> boxes;
    float bodySize = 10.5f;
    layoutBoxes (list, boxes, bodySize);
    for (int i = 0; i < list.size(); ++i)
    {
        const auto& c = list.getReference (i);
        const auto r = boxes[i];
        card (g, r, c.colour.withAlpha (0.8f), 0.97f);
        badge (g, { r.getX() + 17, r.getY() + 17 }, c.n, c.colour);
        g.setColour (c.colour);
        g.setFont (uiFont (12.0f, true));
        g.drawText (c.title, r.getX() + 32, r.getY() + 8, r.getWidth() - 40, 18, juce::Justification::centredLeft, true);
        juce::AttributedString s;
        s.append (c.body, uiFont (bodySize), kBody);
        juce::TextLayout tl;
        tl.createLayout (s, (float) (r.getWidth() - 20));
        tl.draw (g, juce::Rectangle<float> ((float) r.getX() + 10.0f, (float) r.getY() + 28.0f, (float) r.getWidth() - 20.0f, (float) r.getHeight() - 30.0f));
    }
    for (auto& c : list)
        for (int k = 0; k < c.targets.size(); ++k)
        {
            const auto t = c.targets[k];
            if (k > 0 && (t.getWidth() < 40 || t.getHeight() < 8)) continue;       // tiny icons and thin lines: colour is enough
            badge (g, { juce::jmax (9, t.getX() - 4), juce::jmax (9, t.getY() - 4) }, c.n, c.colour);
        }

    g.setColour (kBg);
    g.fillRect (418, 4, 198, 34);
    g.setColour (kBody);
    g.setFont (uiFont (11.0f));
    const char* pageNames[3] = { "Help 1 of 3: the lines", "Help 2 of 3: graph, meters", "Help 3 of 3: the controls" };
    g.drawText (pageNames[page], 424, 9, 190, 24, juce::Justification::centredLeft, false);
}

//==============================================================================  about overlay (logo)
VocalRiderEditor::AboutOverlay::AboutOverlay()
{
    guideBtn.setColour (juce::TextButton::buttonColourId, kAfter);
    guideBtn.setColour (juce::TextButton::textColourOffId, kInk);
    guideBtn.onClick = [this] { if (onGuide) onGuide(); };
    addAndMakeVisible (guideBtn);
}

void VocalRiderEditor::AboutOverlay::resized()
{
    const auto r = cardR();
    guideBtn.setBounds (r.getRight() - 24 - 130, r.getBottom() - 24 - 28, 130, 28);
}

void VocalRiderEditor::AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xe6101010));
    const auto cr = cardR();
    card (g, cr);

    auto r = cr.reduced (26);
    if (logo.isValid())
    {
        auto logoR = r.removeFromLeft (92).removeFromTop (92);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, logoR.toFloat(), juce::RectanglePlacement::centred);
        r.removeFromLeft (20);
    }
    g.setColour (kText);
    g.setFont (uiFont (20.0f, true));
    g.drawText ("VOCALISE-RIDER", r.removeFromTop (28), juce::Justification::centredLeft);
    g.setColour (kAfter);
    g.fillRect (r.removeFromTop (1));
    r.removeFromTop (12);
    g.setColour (kDim);
    g.setFont (uiFont (13.0f));
    g.drawText (juce::String ("Version ") + JucePlugin_VersionString + "  |  Automatic vocal level rider",
                r.removeFromTop (20), juce::Justification::centredLeft);
    r.removeFromTop (14);
    g.setColour (kText);
    g.setFont (uiFont (16.0f, true));
    g.drawText ("Made by Claude and Daemonj Multimedia", r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (6);
    g.setColour (kBody);
    g.setFont (uiFont (12.0f));
    g.drawFittedText ("Free plugin from Daemonj Multimedia. The code was written with Claude, an AI assistant made by Anthropic. "
                      "Built with the JUCE audio framework (juce.com).",
                      r.removeFromTop (60), juce::Justification::topLeft, 4);
    g.setColour (kFaint);
    g.setFont (uiFont (11.0f));
    g.drawText ("Click anywhere to close", cr.reduced (26).removeFromBottom (16).removeFromLeft (200), juce::Justification::centredLeft);
}

//==============================================================================  advanced overlay (cog)
void VocalRiderEditor::AdvancedOverlay::setupRow (Row& r, VocalRiderProcessor& p, const juce::String& title, const juce::String& text,
                                                  const char* id, const juce::String& suffix)
{
    r.name.setText (title, juce::dontSendNotification);
    r.name.setFont (uiFont (13.0f, true));
    r.name.setColour (juce::Label::textColourId, kText);
    r.desc.setText (text, juce::dontSendNotification);
    r.desc.setFont (uiFont (10.5f));
    r.desc.setColour (juce::Label::textColourId, kBody);
    r.desc.setJustificationType (juce::Justification::topLeft);
    r.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    r.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 22);
    r.slider.setTextValueSuffix (suffix);
    r.slider.setNumDecimalPlacesToDisplay (1);
    addAndMakeVisible (r.name);
    addAndMakeVisible (r.desc);
    addAndMakeVisible (r.slider);
    r.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, r.slider);
}

VocalRiderEditor::AdvancedOverlay::AdvancedOverlay (VocalRiderProcessor& p)
{
    lookName.setText ("Lookahead", juce::dontSendNotification);
    lookName.setFont (uiFont (13.0f, true));
    lookName.setColour (juce::Label::textColourId, kText);
    lookDesc.setText ("How far ahead of the audio the ride is placed, so the volume is already in place when a word arrives. "
                      "30 ms suits most vocals. Longer is smoother on fast words. Off reacts after the fact. "
                      "It changes the plugin's delay, so keep Plug-in Delay Compensation on in your DAW.",
                      juce::dontSendNotification);
    lookDesc.setFont (uiFont (10.5f));
    lookDesc.setColour (juce::Label::textColourId, kBody);
    lookDesc.setJustificationType (juce::Justification::topLeft);
    lookBox.addItemList ({ "Off", "10 ms", "20 ms", "30 ms", "50 ms", "80 ms" }, 1);
    addAndMakeVisible (lookName);
    addAndMakeVisible (lookDesc);
    addAndMakeVisible (lookBox);
    lookAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "lookahead", lookBox);

    setupRow (leveling, p, "Leveling",
              "How strongly everything is pulled toward the Target, not only the loud and quiet extremes. 0 = only the Tame Peaks and "
              "Lower Levels lines act. 10 = the whole vocal is pulled to the Target. 4 to 6 is a good start.", "leveling", "");

    autoSet.setColour (juce::TextButton::buttonColourId, kAfter);
    autoSet.setColour (juce::TextButton::textColourOffId, kInk);
    autoSet.onClick = [this] { if (onAutoSet) onAutoSet(); };
    addAndMakeVisible (autoSet);
    autoSet.setTooltip ("Play 10+ seconds of the vocal first. Sets Target, Tame Peaks, Lower Levels and Silence for you.");
    status.setFont (uiFont (11.0f, true));
    status.setColour (juce::Label::textColourId, kTarget);
    status.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (status);
    rideNow.setFont (uiFont (11.0f));
    rideNow.setColour (juce::Label::textColourId, kDim);
    rideNow.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (rideNow);

    closeBtn.setColour (juce::TextButton::buttonColourId, kPanel2);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);
}

void VocalRiderEditor::AdvancedOverlay::resized()
{
    const auto cr = cardR();
    closeBtn.setBounds (cr.getRight() - 20 - 70, cr.getY() + 12, 70, 26);
    rideNow.setBounds (cr.getRight() - 20 - 70 - 10 - 200, cr.getY() + 12, 200, 26);

    int y = cr.getY() + 54;
    const int x = cr.getX() + 22, w = cr.getWidth() - 44;

    lookName.setBounds (x, y, 200, 24);
    lookBox.setBounds (x + w - 130, y, 130, 24);
    lookDesc.setBounds (x, y + 28, w, 44);
    y += 80;

    for (auto* r : { &leveling })
    {
        r->name.setBounds (x, y, 200, 24);
        r->slider.setBounds (x + 200, y, w - 200, 24);
        r->desc.setBounds (x, y + 28, w, 30);
        y += 66;
    }
    autoSet.setBounds (x, y + 2, 100, 28);
    status.setBounds (x + 112, y + 2, w - 112, 40);
}

void VocalRiderEditor::AdvancedOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xe6101010));
    const auto cr = cardR();
    card (g, cr);
    g.setColour (kText);
    g.setFont (uiFont (17.0f, true));
    g.drawText ("ADVANCED", cr.getX() + 22, cr.getY() + 12, 200, 26, juce::Justification::centredLeft);
    g.setColour (kAfter);
    g.fillRect (cr.getX() + 22, cr.getY() + 44, cr.getWidth() - 44, 1);
}

//==============================================================================  editor
VocalRiderEditor::VocalRiderEditor (VocalRiderProcessor& p)
    : AudioProcessorEditor (&p), proc (p), adv (p)
{
    setLookAndFeel (&look);

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    about.logo = logo;

    //---- knobs: SPEED / TARGET / SILENCE
    auto setupKnob = [this] (juce::Slider& s, juce::Label& title, const juce::String& name, const juce::String& caption)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 22);
        s.getProperties().set ("caption", caption);
        title.setText (name, juce::dontSendNotification);
        title.setFont (uiFont (14.0f, true));
        title.setColour (juce::Label::textColourId, kText);
        title.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (s);
        addAndMakeVisible (title);
    };
    setupKnob (sSpeed, lSpeed, "SPEED", "ATTACK/\nRELEASE");
    setupKnob (sTarget, lTarget, "TARGET", "TARGET\nLEVEL");
    setupKnob (sSilence, lSilence, "SILENCE", "HOLD\nBELOW THIS\nLEVEL");
    sSpeed.setNumDecimalPlacesToDisplay (1);
    sTarget.setTextValueSuffix (" dB");
    sTarget.setNumDecimalPlacesToDisplay (1);
    sSilence.setTextValueSuffix (" dB");
    sSilence.setNumDecimalPlacesToDisplay (1);
    sTarget.setRange (-50.0, -5.0, 0.1);
    sTarget.setDoubleClickReturnValue (true, -22.0);
    sSpeed.setDoubleClickReturnValue (true, 5.0);
    sSilence.setDoubleClickReturnValue (true, -50.0);
    sSpeed.setTooltip ("How fast the ride reacts");
    sTarget.setTooltip ("The loudness the vocal is pulled toward. Turning it switches Smart Target off.");
    sSilence.setTooltip ("Below this level counts as breath or room noise and is left alone");
    aSpeed   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "speed", sSpeed);
    aSilence = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "gate", sSilence);

    // The Target knob is wired by hand: with Smart Target on it shows the level being followed, and turning it
    // takes over (Smart Target switches off, starting from where the line is now).
    sTarget.onDragStart = [this]
    {
        targetGesture = true;
        if (param ("autoTarget") > 0.5f) { setParam ("target", currentTarget()); commitParam ("autoTarget", 0.0f); }
        if (auto* pr = proc.apvts.getParameter ("target")) pr->beginChangeGesture();
    };
    sTarget.onDragEnd = [this]
    {
        if (auto* pr = proc.apvts.getParameter ("target")) pr->endChangeGesture();
        targetGesture = false;
    };
    sTarget.onValueChange = [this]
    {
        if (syncingTarget) return;
        const float v = (float) sTarget.getValue();
        if (targetGesture) { setParam ("target", v); return; }
        if (param ("autoTarget") > 0.5f) commitParam ("autoTarget", 0.0f);
        commitParam ("target", v);
    };

    //---- output fader
    sOutput.setSliderStyle (juce::Slider::LinearVertical);
    sOutput.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 20);
    sOutput.setTextValueSuffix (" dB");
    sOutput.setNumDecimalPlacesToDisplay (1);
    sOutput.setDoubleClickReturnValue (true, 0.0);
    sOutput.setTooltip ("Plain volume after the ride. Leave it at 0.0 dB (unity) unless you need to turn the vocal down.");
    addAndMakeVisible (sOutput);
    aOutput = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "output", sOutput);

    //---- - / + buttons
    struct Step { StepButton* b; const char* id; float delta; };
    for (auto s : { Step { &spMinus, "speed", -0.1f }, Step { &spPlus, "speed", 0.1f },
                    Step { &tgMinus, "target", -0.5f }, Step { &tgPlus, "target", 0.5f },
                    Step { &siMinus, "gate", -1.0f }, Step { &siPlus, "gate", 1.0f } })
    {
        addAndMakeVisible (*s.b);
        s.b->onClick = [this, s] { nudge (s.id, s.delta); };
    }

    //---- LIVE / WRITE / READ
    {
        const char* tips[3] = { "Rides the audio live. Nothing is recorded.",
                                "Rides and writes the ride into your DAW's automation (Logic: set the track to Latch or Touch, press play).",
                                "Plays back the ride recorded in your DAW's automation. Nothing is analysed." };
        for (int i = 0; i < 3; ++i)
        {
            modeBtn[i].setTooltip (tips[i]);
            modeBtn[i].onClick = [this, i] { commitParam ("mode", (float) i); };
            addAndMakeVisible (modeBtn[i]);
        }
    }

    //---- power switches
    tSmartTarget.setTooltip ("ON: the plugin finds the target by itself from the singer's average level");
    tSmartSilence.setTooltip ("ON: in pauses and breaths any boost eases back to 0 so room noise is not turned up");
    tProtect.setTooltip ("ON: hissy S and T sounds never move the ride, so they stay natural");
    for (auto* t : { &tSmartTarget, &tSmartSilence, &tProtect }) addAndMakeVisible (*t);
    aSmartTarget  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "autoTarget", tSmartTarget);
    aSmartSilence = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "smartSilence", tSmartSilence);
    aProtect      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "protectS", tProtect);

    //---- header buttons and overlays
    addAndMakeVisible (logoBtn);
    addAndMakeVisible (helpBtn);
    addAndMakeVisible (infoBtn);
    addAndMakeVisible (cogBtn);
    logoBtn.onClick = [this] { showAbout(); };
    helpBtn.onClick = [this] { showHelpPage (0); };
    infoBtn.onClick = [this] { openGuide(); };
    cogBtn.onClick  = [this] { showAdvanced(); };

    help.getCallouts = [this] (int page) { return buildCallouts (page); };
    help.onClose  = [this] { help.setVisible (false); };
    about.onClose = [this] { about.setVisible (false); };
    about.onGuide = [this] { about.setVisible (false); openGuide(); };
    adv.onClose   = [this] { adv.setVisible (false); };
    adv.onAutoSet = [this] { runAutoSet(); };
    addChildComponent (help);
    addChildComponent (about);
    addChildComponent (adv);

    setSize (733, 525);
    startTimerHz (24);
}

VocalRiderEditor::~VocalRiderEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VocalRiderEditor::showHelpPage (int page) { help.setPage (page); help.setVisible (true); help.toFront (true); }
void VocalRiderEditor::showAdvanced()          { adv.setVisible (true); adv.toFront (true); }
void VocalRiderEditor::showAbout()             { about.setVisible (true); about.toFront (true); }

void VocalRiderEditor::openGuide()
{
    // The guide is a web page built into the plugin: write it to a temporary file and open it in the browser.
    auto html = juce::String::fromUTF8 (BinaryData::guide_html, BinaryData::guide_htmlSize);
    html = html.replace ("{{VERSION}}", JucePlugin_VersionString)
               .replace ("{{LOGO}}", "data:image/png;base64," + juce::Base64::toBase64 (BinaryData::logo_png, (size_t) BinaryData::logo_pngSize));
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Vocalise-Rider-Guide.html");
    if (f.replaceWithText (html))
        if (! juce::URL (f).launchInDefaultBrowser())
            f.startAsProcess();
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

void VocalRiderEditor::nudge (const char* id, float delta)
{
    auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id));
    if (p == nullptr) return;
    const bool isTarget = juce::String (id) == "target";
    const float base = isTarget ? currentTarget() : param (id);
    const auto range = p->getNormalisableRange();
    const float v = juce::jlimit (range.start, range.end, base + delta);
    if (isTarget && param ("autoTarget") > 0.5f) commitParam ("autoTarget", 0.0f);   // adjusting it by hand = "I'll set it"
    commitParam (id, v);
}

//==============================================================================  layout
void VocalRiderEditor::resized()
{
    about.setBounds (getLocalBounds());
    help.setBounds (getLocalBounds());
    adv.setBounds (getLocalBounds());

    logoBtn.setBounds (8, 4, 150, 34);
    cogBtn.setBounds (702 - 12, 8, 24, 24);
    infoBtn.setBounds (667 - 12, 8, 24, 24);
    helpBtn.setBounds (634 - 12, 8, 24, 24);

    graphCard = { 14, 46, 589, 306 };
    outCard   = { 611, 46, 108, 306 };
    botCard   = { 14, 364, 705, 148 };
    stripArea = { graphCard.getX() + 10, graphCard.getBottom() - 52, graphCard.getWidth() - 20, 44 };
    meterArea = { outCard.getX() + 66, outCard.getY() + 32, 40, 262 };

    sOutput.setBounds (outCard.getX() + 8, outCard.getY() + 30, 56, 240);

    const int knobCx[3] = { botCard.getX() + 62, botCard.getX() + 166, botCard.getX() + 270 };
    juce::Slider* knobs[3] = { &sSpeed, &sTarget, &sSilence };
    juce::Label* titles[3] = { &lSpeed, &lTarget, &lSilence };
    StepButton* minus[3] = { &spMinus, &tgMinus, &siMinus };
    StepButton* plus[3]  = { &spPlus, &tgPlus, &siPlus };
    for (int i = 0; i < 3; ++i)
    {
        const int cx = knobCx[i];
        titles[i]->setBounds (cx - 50, botCard.getY() + 9, 100, 20);
        knobs[i]->setBounds (cx - 48, botCard.getY() + 32, 96, 102);
        const int cy = botCard.getY() + 32 + 40;
        minus[i]->setBounds (cx - 43 - 9, cy + 26, 18, 18);
        plus[i]->setBounds (cx + 43 - 9, cy + 26, 18, 18);
    }

    const int rowY[3] = { botCard.getY() + 13, botCard.getY() + 59, botCard.getY() + 105 };
    for (int i = 0; i < 3; ++i)
    {
        modeBtn[i].setBounds (349, rowY[i], 84, 27);
        PowerToggle* t[3] = { &tSmartTarget, &tSmartSilence, &tProtect };
        t[i]->setBounds (606, rowY[i], 104, 27);
    }
}

//==============================================================================  graph helpers
juce::Rectangle<float> VocalRiderEditor::plotRect() const
{
    return { (float) graphCard.getX() + 40.0f, (float) graphCard.getY() + 8.0f,
             (float) graphCard.getWidth() - 50.0f, 242.0f };
}

float VocalRiderEditor::dbToY (float db) const
{
    const auto p = plotRect();
    const float c = juce::jlimit (kDbBot, kDbTop, db);
    return p.getY() + (kDbTop - c) / (kDbTop - kDbBot) * p.getHeight();
}

float VocalRiderEditor::yToDb (float y) const
{
    const auto p = plotRect();
    const float f = (y - p.getY()) / p.getHeight();
    return juce::jlimit (kDbBot, kDbTop, kDbTop - f * (kDbTop - kDbBot));
}

juce::Rectangle<int> VocalRiderEditor::tagRect (float y) const
{
    const auto p = plotRect();
    return juce::Rectangle<float> (p.getX() + 4.0f, y - 10.0f, 146.0f, 20.0f).toNearestInt();
}

VocalRiderEditor::Line VocalRiderEditor::lineAt (juce::Point<int> pt) const
{
    if (! plotRect().expanded (0.0f, 4.0f).contains (pt.toFloat())) return Line::none;
    const float tgt = currentTarget();
    const float yT = dbToY (tgt);
    const float yL = dbToY (tgt + param ("loudAbove"));
    const float yQ = dbToY (tgt - param ("quietBelow"));
    const float yS = dbToY (param ("gate"));
    const float y = (float) pt.y;
    const float dT = std::abs (y - yT), dL = std::abs (y - yL), dQ = std::abs (y - yQ), dS = std::abs (y - yS);
    const float best = std::min (std::min (dT, dL), std::min (dQ, dS));
    if (best > 7.0f) return Line::none;
    if (dL == best && dQ == best) return y < yT ? Line::loud : Line::quiet;   // both at the target line
    if (dL == best) return Line::loud;
    if (dQ == best) return Line::quiet;
    if (dT == best) return Line::target;
    return Line::silence;
}

void VocalRiderEditor::mouseMove (const juce::MouseEvent& e)
{
    hover = lineAt (e.getPosition());
    setMouseCursor (hover == Line::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
}

static const char* idForLine (int l)
{
    switch (l) { case 1: return "loudAbove"; case 2: return "target"; case 3: return "quietBelow"; default: return "gate"; }
}

void VocalRiderEditor::mouseDown (const juce::MouseEvent& e)
{
    if (meterArea.contains (e.getPosition()))                  // click the meter to clear the clip light and the hold marker
    {
        clipped = false;
        peakHold[0] = peakHold[1] = -100.0f;
        return;
    }
    dragging = lineAt (e.getPosition());
    if (dragging == Line::none) return;
    if (auto* p = proc.apvts.getParameter (idForLine ((int) dragging))) p->beginChangeGesture();
    if (dragging == Line::target)
    {
        // grabbing the target line means "I'll set it myself": start from where it is now, then switch Smart Target off
        const float startAt = currentTarget();
        setParam ("target", startAt);
        commitParam ("autoTarget", 0.0f);
    }
}

void VocalRiderEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Line::none) return;
    const float db = yToDb ((float) e.y);
    if (dragging == Line::target)       setParam ("target", db);
    else if (dragging == Line::silence) setParam ("gate", db);
    else if (dragging == Line::loud)    setParam ("loudAbove", juce::jlimit (0.0f, 24.0f, db - currentTarget()));
    else                                setParam ("quietBelow", juce::jlimit (0.0f, 24.0f, currentTarget() - db));
}

void VocalRiderEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Line::none) return;
    if (auto* p = proc.apvts.getParameter (idForLine ((int) dragging))) p->endChangeGesture();
    dragging = Line::none;
}

//==============================================================================  AUTO SET
void VocalRiderEditor::runAutoSet()
{
    uint32_t h[RiderCore::kHistBins];
    proc.getCore().readLevelHistogram (h);
    uint64_t total = 0;
    for (auto v : h) total += v;
    auto say = [this] (const juce::String& s) { autoStatus = s; adv.status.setText (s, juce::dontSendNotification); };
    if (total < 500)
    {
        say ("Play at least 5 seconds of the vocal first, then click AUTO SET.");
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
        say ("Not enough vocal above the silence level yet. Play more of the vocal and try again.");
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

    say ("Set from " + juce::String ((double) total / 100.0, 0) + " s of vocal: target "
         + juce::String (p50, 1) + " dB, silence " + juce::String (gate, 0) + " dB.");
    proc.getCoreForEditor().clearLevelHistogram();                // next AUTO SET starts fresh
}

//==============================================================================  timer: copy data, preview the ride, meters
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

int VocalRiderEditor::lookaheadFrames() const
{
    const int idx = juce::jlimit (0, 5, (int) std::lround (param ("lookahead")));
    return (int) std::lround (VocalRiderProcessor::kLookaheadChoicesMs[idx] / (RiderCore::kHopSeconds * 1000.0));
}

void VocalRiderEditor::timerCallback()
{
    proc.getCore().readHistory (before, level, sib);
    shownTarget = proc.getCore().getShownTargetDb();

    // ---- AFTER RIDING preview: the same decisions the plugin makes, re-run on the stored 10 s of vocal
    //      with your CURRENT settings, so moving a line or knob reshapes the green trace right away. ----
    const auto p = currentParams();
    RiderCore::simulate (level, sib, N, p, currentTarget(), gainDb);
    const int ahead = lookaheadFrames();
    for (int i = 0; i < N; ++i)
    {
        if (before[i] < -99.0f) { after[i] = -100.0f; continue; }
        const int j = std::min (i + ahead, N - 1);                         // the ride leads the audio by the lookahead
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

    // ---- keep the Target knob in step with the line (it shows the learned level while Smart Target is on) ----
    if (! targetGesture && ! sTarget.isParentOf (juce::Component::getCurrentlyFocusedComponent()))
    {
        syncingTarget = true;
        sTarget.setValue (currentTarget(), juce::dontSendNotification);
        syncingTarget = false;
    }

    float atk, rel;
    speedToMs (p.speed, atk, rel);
    auto ms = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " s" : juce::String ((int) std::lround (v)) + " ms"; };
    sSpeed.setTooltip ("How fast the ride reacts. Loud parts come down in about " + ms (atk) + ", quiet parts come back up in about " + ms (rel) + ".");

    // ---- ride mode buttons ----
    {
        const int mode = juce::jlimit (0, 2, (int) std::lround (param ("mode")));
        for (int i = 0; i < 3; ++i) modeBtn[i].active = (i == mode), modeBtn[i].repaint();
    }
    if (adv.isVisible())
        adv.rideNow.setText ("Current ride: " + signedDb (proc.getCore().getCurrentRideDb()), juce::dontSendNotification);

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

//==============================================================================  help content
juce::Array<VocalRiderEditor::Callout> VocalRiderEditor::buildCallouts (int page) const
{
    juce::Array<Callout> out;
    const float tgt = currentTarget(), loudOff = param ("loudAbove"), quietOff = param ("quietBelow");
    const auto pr = plotRect();
    auto lineStrip = [&] (float y) { return juce::Rectangle<int> ((int) pr.getX(), (int) y - 2, (int) pr.getWidth(), 4); };
    const float yL = dbToY (tgt + loudOff), yQ = dbToY (tgt - quietOff), yT = dbToY (tgt), yS = dbToY (param ("gate"));

    if (page == 0)
    {
        out.add ({ 1, "TAME PEAKS (purple line)", "The ceiling for loud parts. The number is how far above the Target it sits: right now "
                   + signedDb (loudOff) + " means " + juce::String (loudOff, 1) + " dB louder than Target. Anything that reaches higher is pulled "
                   "back down to this line. Drag it down to tame more.", kLoud, { tagRect (yL), lineStrip (yL) }, 0 });
        out.add ({ 2, "LOWER LEVELS (blue line)", "The floor for quiet parts. The number is how far below the Target it sits: right now "
                   + signedDb (-quietOff) + " means " + juce::String (quietOff, 1) + " dB quieter than Target. Anything lower is lifted up to this "
                   "line. Drag it up to lift more.", kQuiet, { tagRect (yQ), lineStrip (yQ) }, 0 });
        out.add ({ 3, "TARGET (teal line)", "The loudness you are aiming for. The vocal is pulled toward this line, and the two lines above and "
                   "below it move with it. Drag it, or turn the Target knob.", kTarget, { lineStrip (yT) }, 1 });
        out.add ({ 4, "SILENCE (grey line)", "Everything under this line is breath or room noise. The plugin leaves it alone so noise is never "
                   "turned up. Drag it, or turn the Silence knob.", kBefore, { lineStrip (yS) }, 1 });
    }
    else if (page == 1)
    {
        out.add ({ 1, "THE GRAPH", "The last 10 seconds of the vocal. Grey is the original, green is after riding. You want the green to be "
                   "flatter than the grey. It updates live as you move the lines and knobs.", kAfter, { juce::Rectangle<int> (pr.toNearestInt()) }, 0 });
        out.add ({ 2, "THE NUMBERS", "LOUD PEAKS and QUIET PARTS are the typical loud and quiet levels in dB: grey before, green after. "
                   "GAP is loud minus quiet. A smaller gap after riding means a more even vocal.", kTarget, { stripArea }, 1 });
        out.add ({ 3, "OUTPUT AND PEAK", "OUTPUT is a plain volume after the ride. Leave it at 0.0 dB (unity) and only turn it down if you must. "
                   "PEAK shows the loudest output level. The red light means it clipped. Click the meter to clear.", kWarn,
                   { outCard }, 2 });
    }
    else
    {
        out.add ({ 1, "SPEED", "How fast the ride reacts, 0 to 10. Low is slow and smooth, high is quick and tight. Hover the knob to see the real "
                   "attack and release times. Use the - and + to nudge it.", kText,
                   { lSpeed.getBounds().getUnion (sSpeed.getBounds()).getUnion (spMinus.getBounds()).getUnion (spPlus.getBounds()) }, 0 });
        out.add ({ 2, "TARGET AND SILENCE KNOBS", "TARGET sets the level the vocal is pulled toward, so it moves the TARGET line. SILENCE sets where breath "
                   "and noise begin, so it moves the SILENCE line. Turning TARGET switches Smart Target off. Click a number to type one.", kTarget,
                   { lTarget.getBounds().getUnion (sTarget.getBounds()).getUnion (tgMinus.getBounds()).getUnion (tgPlus.getBounds()),
                     lSilence.getBounds().getUnion (sSilence.getBounds()).getUnion (siMinus.getBounds()).getUnion (siPlus.getBounds()) }, 0 });
        out.add ({ 3, "LIVE, WRITE, READ", "LIVE rides while you play and records nothing. WRITE records the ride into your DAW's automation (Logic: "
                   "Latch or Touch, then play). READ plays that recorded ride back so you can edit it, then bounce.", kLoud,
                   { modeBtn[0].getBounds().getUnion (modeBtn[2].getBounds()) }, 1 });
        out.add ({ 4, "THE THREE SWITCHES", "SMART TARGET: finds the target by itself from the singer's average level. SMART SILENCE: in pauses any boost "
                   "eases back to 0, so noise is not turned up. PROTECT \"S\" SOUNDS: hissy S and T sounds never move the ride.", kAfter,
                   { tSmartTarget.getBounds(), tSmartSilence.getBounds(), tProtect.getBounds() }, 1 });
        out.add ({ 5, "TOP RIGHT AND LOGO", "The logo opens About. ? is this help. i opens the full guide in your browser. The cog opens ADVANCED: "
                   "lookahead, leveling and AUTO SET.", kQuiet,
                   { logoBtn.getBounds(), helpBtn.getBounds(), infoBtn.getBounds(), cogBtn.getBounds() }, 2 });
    }
    return out;
}

//==============================================================================  paint
void VocalRiderEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // ---------- header ----------
    g.setColour (kGrid);
    g.fillRect (0, 41, getWidth(), 1);
    if (logo.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, juce::Rectangle<float> (14.0f, 6.0f, 30.0f, 30.0f), juce::RectanglePlacement::centred);
    }
    {
        auto f = uiFont (12.5f, true);
        f.setExtraKerningFactor (0.06f);
        auto f2 = uiFont (12.5f);
        f2.setExtraKerningFactor (0.06f);
        const int w1 = (int) std::ceil (f.getStringWidth ("VOCALISE")), w2 = (int) std::ceil (f2.getStringWidth ("RIDER"));
        const int gapPx = 9, x1 = 50, xDiv = x1 + w1 + gapPx, x2 = xDiv + 1 + gapPx;      // divider sits exactly midway
        g.setFont (f);
        g.setColour (kText);
        g.drawText ("VOCALISE", x1, 10, w1 + 2, 22, juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff444444));
        g.fillRect (xDiv, 12, 1, 18);
        g.setFont (f2);
        g.setColour (juce::Colour (0xffb0b0b0));
        g.drawText ("RIDER", x2, 10, w2 + 2, 22, juce::Justification::centredLeft, false);
    }
    {   // legend
        auto f = uiFont (10.0f);
        f.setExtraKerningFactor (0.06f);
        g.setFont (f);
        auto legend = [&] (int x, const juce::String& t, juce::Colour c)
        {
            g.setColour (c);
            g.fillRoundedRectangle ((float) x, 19.0f, 26.0f, 2.5f, 1.2f);
            g.setColour (kDim);
            g.drawText (t, x + 32, 11, 110, 18, juce::Justification::centredLeft, false);
        };
        legend (400, "ORIGINAL", kBefore);
        legend (498, "AFTER RIDING", kAfter);
    }

    const auto plot = plotRect();
    const float x0 = plot.getX(), x1 = plot.getRight();

    // ---------- level graph ----------
    card (g, graphCard);
    g.setFont (uiFont (9.5f));
    for (int d = -10; d >= -50; d -= 10)
    {
        g.setColour (kGrid);
        g.drawHorizontalLine ((int) dbToY ((float) d), x0, x1);
        g.setColour (kDim);
        g.drawText (juce::String (d), graphCard.getX() + 4, (int) dbToY ((float) d) - 7, 28, 14, juce::Justification::centredRight, false);
    }

    const float tgt = currentTarget();
    const float loudOff = param ("loudAbove"), quietOff = param ("quietBelow");
    const float yT = dbToY (tgt), yL = dbToY (tgt + loudOff), yQ = dbToY (tgt - quietOff), yG = dbToY (param ("gate"));

    g.setColour (kAfter.withAlpha (0.045f));                  // the zone between the two lines
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
        g.strokePath (path, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    drawTrace (before, kBefore, 1.2f);
    drawTrace (after, kAfter, 2.0f);

    // the four draggable lines (dashed) with small captions
    auto dashedLine = [&] (float y, juce::Colour c, bool active)
    {
        const float dashes[] = { 5.0f, 4.0f };
        g.setColour (c.withAlpha (active ? 1.0f : 0.9f));
        g.drawDashedLine (juce::Line<float> (x0, y, x1, y), dashes, 2, active ? 2.0f : 1.3f);
    };
    auto smallCaption = [&] (const juce::String& t, float y, juce::Colour c)
    {
        auto f = uiFont (8.0f, true);
        f.setExtraKerningFactor (0.05f);
        g.setFont (f);
        g.setColour (c);
        g.drawText (t, (int) x1 - 62, (int) y - 11, 60, 10, juce::Justification::centredRight, false);
    };
    dashedLine (yG, kBefore, hover == Line::silence || dragging == Line::silence);
    smallCaption ("SILENCE", yG, kBefore);
    dashedLine (yT, kTarget, hover == Line::target || dragging == Line::target);
    smallCaption ("TARGET", yT, kTarget);
    dashedLine (yL, kLoud, hover == Line::loud || dragging == Line::loud);
    dashedLine (yQ, kQuiet, hover == Line::quiet || dragging == Line::quiet);

    auto drawTag = [&] (float y, juce::Colour c, const juce::String& name, const juce::String& value, bool up)
    {
        const auto r = tagRect (y).toFloat();
        g.setColour (kPanel.interpolatedWith (c, 0.16f));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (c);
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.1f);
        juce::Path tri;                                       // little arrow: down = pull down, up = lift up
        const float ax = r.getX() + 11.0f, ay = r.getCentreY();
        if (up) tri.addTriangle (ax - 4.0f, ay + 3.0f, ax + 4.0f, ay + 3.0f, ax, ay - 3.5f);
        else    tri.addTriangle (ax - 4.0f, ay - 3.0f, ax + 4.0f, ay - 3.0f, ax, ay + 3.5f);
        g.fillPath (tri);
        auto f = uiFont (8.5f, true);
        f.setExtraKerningFactor (0.04f);
        g.setFont (f);
        g.drawText (name, (int) r.getX() + 20, (int) r.getY(), 90, (int) r.getHeight(), juce::Justification::centredLeft, false);
        g.drawText (value, (int) r.getRight() - 56, (int) r.getY(), 50, (int) r.getHeight(), juce::Justification::centredRight, false);
    };
    drawTag (yL, kLoud, "TAME PEAKS", signedDb (loudOff).replace (" dB", "") + " dB", false);
    drawTag (yQ, kQuiet, "LOWER LEVELS", signedDb (-quietOff).replace (" dB", "") + " dB", true);

    // ---------- numbers strip (original vs after) ----------
    {
        const auto s = stripArea;
        g.setColour (juce::Colour (0xff1c1c1c));
        g.fillRoundedRectangle (juce::Rectangle<int> (s.getX(), s.getY(), 300, s.getHeight()).toFloat(), 6.0f);
        const char* names[3] = { "LOUD PEAKS", "QUIET PARTS", "GAP (LOUD - QUIET)" };
        const juce::Colour nameCol[3] = { kLoud, kQuiet, juce::Colour (0xffb0b0b0) };
        auto f = uiFont (9.0f, true);
        f.setExtraKerningFactor (0.05f);
        for (int r = 0; r < 3; ++r)
        {
            const int ry = s.getY() + 4 + r * 13;
            g.setFont (f);
            g.setColour (nameCol[r]);
            g.drawText (names[r], s.getX() + 10, ry, 130, 13, juce::Justification::centredLeft, false);
            g.setFont (uiFont (9.5f));
            g.setColour (kBefore);
            g.drawText (haveStats ? juce::String (statBefore[r], 1) + " dB" : "-", s.getX() + 130, ry, 70, 13, juce::Justification::centredRight, false);
            g.setColour (kAfter);
            g.drawText (haveStats ? juce::String (statAfter[r], 1) + " dB" : "-", s.getX() + 210, ry, 70, 13, juce::Justification::centredRight, false);
        }
        g.setColour (kBody);
        g.setFont (uiFont (10.0f));
        g.drawFittedText ("The gap is how far apart the loud and quiet parts are. A smaller gap means a more even waveform.",
                          juce::Rectangle<int> (s.getX() + 312, s.getY(), s.getWidth() - 312, s.getHeight()),
                          juce::Justification::centredLeft, 3, 1.0f);
    }

    // ---------- output + peak card ----------
    card (g, outCard);
    {
        g.setColour (kText);
        auto f = uiFont (10.0f, true);
        f.setExtraKerningFactor (0.03f);
        g.setFont (f);
        g.drawText ("OUTPUT", outCard.getX() + 6, outCard.getY() + 9, 60, 16, juce::Justification::centred, false);
        g.drawText ("PEAK", outCard.getX() + 66, outCard.getY() + 9, 40, 16, juce::Justification::centred, false);
        g.setColour (kFaint);
        g.setFont (uiFont (7.5f, true));
        g.drawFittedText ("MASTER\nOUTPUT", juce::Rectangle<int> (outCard.getX() + 6, outCard.getBottom() - 34, 60, 24),
                          juce::Justification::centred, 2, 1.0f);

        const auto m = meterArea;
        const float top = (float) m.getY() + 22.0f, bot = (float) m.getY() + 208.0f;
        auto yOf = [&] (float db) { return bot - (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f * (bot - top); };
        juce::ColourGradient grad (kClip, 0.0f, top, kAfter, 0.0f, bot, false);
        grad.addColour (0.03, kClip);
        grad.addColour (0.08, kWarn);                         // about -5 dB
        grad.addColour (0.20, kAfter);                        // about -12 dB
        for (int c = 0; c < 2; ++c)
        {
            const juce::Rectangle<float> bar ((float) m.getX() + 2.0f + (float) c * 17.0f, top, 13.0f, bot - top);
            g.setColour (kClip.withAlpha (clipped ? 1.0f : 0.35f));                  // clip light
            g.fillRoundedRectangle (bar.getX(), top - 9.0f, bar.getWidth(), 4.0f, 1.5f);
            g.setColour (juce::Colour (0xff1c1c1c));
            g.fillRoundedRectangle (bar, 2.0f);
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> (bar.getX(), yOf (peakShown[c]), bar.getWidth(), bot - yOf (peakShown[c])));
            if (peakHold[c] > -59.0f)
            {
                g.setColour (peakHold[c] > -1.0f ? kClip : juce::Colour (0xffc8c8c8));
                g.fillRect (bar.getX(), yOf (peakHold[c]) - 1.0f, bar.getWidth(), 2.0f);
            }
        }
        const float hold = std::max (peakHold[0], peakHold[1]);
        g.setFont (uiFont (9.0f, true));
        g.setColour (clipped ? kClip : kText);
        g.drawText (hold > -99.0f ? signedDb (hold) : juce::String ("-inf dB"), m.getX() - 4, (int) bot + 8, m.getWidth() + 8, 14, juce::Justification::centred, false);
        g.setColour (kClip);
        g.setFont (uiFont (7.5f, true));
        g.drawFittedText ("CLICK TO\nCLEAR", juce::Rectangle<int> (m.getX(), (int) bot + 24, m.getWidth(), 22), juce::Justification::centred, 2, 1.0f);
    }

    // ---------- bottom card ----------
    card (g, botCard);
    g.setColour (juce::Colour (0xff383838));
    g.fillRect (446, botCard.getY() + 16, 1, botCard.getHeight() - 32);
    {
        const int rowY[3] = { botCard.getY() + 13, botCard.getY() + 59, botCard.getY() + 105 };
        const char* names[3] = { "SMART TARGET", "SMART SILENCE", "PROTECT \"S\" SOUNDS" };
        g.setColour (kText);
        auto f = uiFont (12.0f, true);
        f.setExtraKerningFactor (0.02f);
        g.setFont (f);
        for (int i = 0; i < 3; ++i)
            g.drawText (names[i], 452, rowY[i], 148, 27, juce::Justification::centredRight, false);
    }
}
