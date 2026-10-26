#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    const juce::Colour background { 0xff0b0c0f };
    const juce::Colour panel { 0xff131419 };
    const juce::Colour raised { 0xff1b1d23 };
    const juce::Colour outline { 0xff272a33 };
    const juce::Colour text { 0xffeceae4 };
    const juce::Colour dim { 0xff7d808a };
    const juce::Colour faint { 0xff4a4d57 };

    const juce::Colour modules[] { juce::Colour (0xffffb347),   // volume: amber
                                   juce::Colour (0xff6fd3c6),   // pan: teal
                                   juce::Colour (0xffb18cff),   // width: violet
                                   juce::Colour (0xff5fa8ff),   // filter: blue
                                   juce::Colour (0xffff7a59),   // drive: coral
                                   juce::Colour (0xffc2e05a),   // crush: lime
                                   juce::Colour (0xffff74b8) }; // time: pink

    const juce::Colour accent = modules[0];

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font caps (float height)
    {
        return font (height, true).withExtraKerningFactor (0.12f);
    }
}

class ContourLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ContourLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (13.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override { return Theme::font (14.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Theme::caps (11.0f); }
};
