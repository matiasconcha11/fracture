#include "LookAndFeel.h"

ContourLookAndFeel::ContourLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
    setColour (juce::Slider::rotarySliderFillColourId, Theme::modules[0]);
    setColour (juce::Slider::textBoxTextColourId, Theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, Theme::modules[0].withAlpha (0.3f));
    setColour (juce::Label::textColourId, Theme::text);
    setColour (juce::TextButton::buttonColourId, Theme::raised);
    setColour (juce::TextButton::buttonOnColourId, Theme::modules[0]);
    setColour (juce::TextButton::textColourOffId, Theme::dim);
    setColour (juce::TextButton::textColourOnId, Theme::text);
    setColour (juce::ComboBox::backgroundColourId, Theme::raised);
    setColour (juce::ComboBox::outlineColourId, Theme::outline);
    setColour (juce::ComboBox::textColourId, Theme::text);
    setColour (juce::ComboBox::arrowColourId, Theme::dim);
    setColour (juce::PopupMenu::backgroundColourId, Theme::raised);
    setColour (juce::PopupMenu::textColourId, Theme::text);
    setColour (juce::PopupMenu::headerTextColourId, Theme::dim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::outline);
    setColour (juce::PopupMenu::highlightedTextColourId, Theme::text);
    setColour (juce::TextEditor::backgroundColourId, Theme::background);
    setColour (juce::TextEditor::textColourId, Theme::text);
    setColour (juce::TextEditor::outlineColourId, Theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, Theme::modules[0]);
    setColour (juce::CaretComponent::caretColourId, Theme::text);
}

void ContourLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const auto radius = size * 0.5f;
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto enabled = slider.isEnabled();
    const auto arcRadius = radius - 3.0f;
    const auto angle = startAngle + sliderPos * (endAngle - startAngle);

    // Track and value arc. Bipolar knobs (Input/Output) draw from the centre.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (Theme::outline);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);
    const auto arcStart = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (angle - arcStart) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (arcStart, angle), juce::jmax (arcStart, angle), true);
        g.setColour (enabled ? accent : Theme::faint);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knob body.
    const auto bodyRadius = arcRadius - 6.0f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (Theme::raised.brighter (0.15f), body.getTopLeft(),
                                             Theme::panel, body.getBottomRight(), false));
    g.fillEllipse (body);
    g.setColour (Theme::outline);
    g.drawEllipse (body, 1.0f);

    // Pointer.
    const auto inner = centre.getPointOnCircumference (bodyRadius * 0.35f, angle);
    const auto outer = centre.getPointOnCircumference (bodyRadius * 0.85f, angle);
    g.setColour (enabled ? Theme::text : Theme::dim);
    g.drawLine ({ inner, outer }, 2.0f);
}

juce::Label* ContourLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setColour (juce::Label::textColourId, Theme::text);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return label;
}

juce::Font ContourLookAndFeel::getLabelFont (juce::Label& label)
{
    // Knob value boxes all share one size, however they were created.
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
        return Theme::font (14.0f);

    return label.getFont();
}

void ContourLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();
    const auto accent = button.findColour (juce::TextButton::buttonOnColourId);

    auto fill = on ? accent.withAlpha (0.18f) : Theme::raised;

    if (down)
        fill = fill.brighter (0.15f);
    else if (highlighted)
        fill = fill.brighter (0.07f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (on ? accent.withAlpha (0.7f) : Theme::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void ContourLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto on = button.getToggleState();
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (on ? button.findColour (juce::TextButton::buttonOnColourId).brighter (0.3f)
                    : button.findColour (juce::TextButton::textColourOffId));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void ContourLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    // A power switch: ring when off, lit dot when on.
    const auto accent = button.findColour (juce::ToggleButton::tickColourId);
    const auto area = button.getLocalBounds().toFloat();
    const auto size = juce::jmin (area.getWidth(), area.getHeight()) - 4.0f;
    const auto dot = juce::Rectangle<float> (size, size).withCentre (area.getCentre());

    if (button.getToggleState())
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillEllipse (dot.expanded (2.0f));
        g.setColour (accent);
        g.fillEllipse (dot.reduced (2.0f));
    }
    else
    {
        g.setColour (highlighted ? Theme::dim : Theme::faint);
        g.drawEllipse (dot.reduced (2.0f), 1.5f);
    }
}

void ContourLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                       juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    juce::Path arrow;
    const auto cx = (float) width - 12.0f;
    const auto cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void ContourLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
}
