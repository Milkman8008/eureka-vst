#include "EurekaLookAndFeel.h"

#include "Skin.h"

namespace ambika_plugin
{
    namespace Palette
    {
        juce::Colour background, panel, panelRaised, outline, track, text, dimText, faintText;

        namespace
        {
            Theme currentTheme = Theme::Dark;

            struct Initialiser
            {
                Initialiser() { setTheme (Theme::Dark); }
            } initialiser;
        }

        void setTheme (Theme newTheme)
        {
            currentTheme = newTheme;
            if (newTheme == Theme::Dark)
            {
                background = juce::Colour (0xff121419);
                panel = juce::Colour (0xff1b1e25);
                panelRaised = juce::Colour (0xff232731);
                outline = juce::Colour (0xff2e3440);
                track = juce::Colour (0xff343b48);
                text = juce::Colour (0xffdde1e8);
                dimText = juce::Colour (0xff858e9e);
                faintText = juce::Colour (0xff5b6372);
            }
            else
            {
                background = juce::Colour (0xffe9ecf1);
                panel = juce::Colour (0xfff8f9fb);
                panelRaised = juce::Colour (0xffeef1f5);
                outline = juce::Colour (0xffcfd5de);
                track = juce::Colour (0xffd9dee6);
                text = juce::Colour (0xff1c2028);
                dimText = juce::Colour (0xff5d6676);
                faintText = juce::Colour (0xff98a0ad);
            }
        }

        Theme theme() { return currentTheme; }

        juce::Colour part (int index)
        {
            static const juce::Colour dark[] = {
                juce::Colour (0xfff0a33a),  // amber
                juce::Colour (0xff3cc8b4),  // teal
                juce::Colour (0xfff0647a),  // coral
                juce::Colour (0xffa6d84a),  // lime
                juce::Colour (0xff5aa8f0),  // sky
                juce::Colour (0xffb08cf5),  // violet
            };
            // Deeper versions, readable on a light background.
            static const juce::Colour light[] = {
                juce::Colour (0xffd8860f), juce::Colour (0xff169a88), juce::Colour (0xffd8435d),
                juce::Colour (0xff5f9417), juce::Colour (0xff2a78cc), juce::Colour (0xff8458d6),
            };
            const auto i = (size_t) juce::jlimit (0, 5, index);
            if (const auto& custom = currentSkin().settings.partColours)
                return (*custom)[i];
            return currentTheme == Theme::Dark ? dark[i] : light[i];
        }
    }

    bool isBipolar (const juce::Slider& slider)
    {
        return slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    }

    EurekaLookAndFeel::EurekaLookAndFeel()
    {
        applyTheme();
    }

    void EurekaLookAndFeel::applyTheme()
    {
        setColourScheme ({ Palette::background, Palette::panel, Palette::panelRaised, Palette::dimText,
                           Palette::text, Palette::part (0), Palette::background, Palette::part (0),
                           Palette::text });
        setColour (juce::ComboBox::backgroundColourId, Palette::panelRaised);
        setColour (juce::ComboBox::outlineColourId, Palette::outline);
        setColour (juce::ComboBox::textColourId, Palette::text);
        setColour (juce::PopupMenu::backgroundColourId, Palette::panelRaised);
        setColour (juce::PopupMenu::textColourId, Palette::text);
        setColour (juce::Label::textColourId, Palette::text);
        setColour (juce::Slider::textBoxTextColourId, Palette::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, Palette::outline);
        setColour (juce::TextEditor::backgroundColourId, Palette::background);
        setColour (juce::TextEditor::outlineColourId, Palette::outline);
        setColour (juce::TextEditor::focusedOutlineColourId, Palette::outline);
        setColour (juce::TextButton::buttonColourId, Palette::panelRaised);
        setColour (juce::TextButton::textColourOffId, Palette::dimText);
        setColour (juce::TextButton::textColourOnId, Palette::background);
        setAccent (Palette::part (0));
    }

    void EurekaLookAndFeel::setAccent (juce::Colour colour)
    {
        accentColour = colour;
        setColour (juce::Slider::rotarySliderFillColourId, colour);
        setColour (juce::Slider::thumbColourId, colour);
        setColour (juce::Slider::trackColourId, colour);
        setColour (juce::ComboBox::arrowColourId, colour);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colour.withAlpha (0.3f));
        setColour (juce::TextButton::buttonOnColourId, colour);
    }

    void EurekaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float position, float startAngle, float endAngle,
                                              juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineWidth = juce::jmax (2.5f, radius * 0.14f);
        const float arcRadius = radius - lineWidth * 0.5f;
        const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);

        const float valueAngle = startAngle + position * (endAngle - startAngle);
        const auto& skin = currentSkin();

        if (skin.knob.isValid() && ! skin.settings.knobValueArc)
        {
            // Filmstrip only: the image is the whole knob.
            const int frame = juce::jlimit (0, skin.knobFrames - 1, juce::roundToInt (position * (float) (skin.knobFrames - 1)));
            const int size = skin.knob.getWidth();
            const auto dest = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre).toNearestInt();
            g.drawImage (skin.knob, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                         0, frame * size, size, size);
            return;
        }

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::track);
        g.strokePath (track, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        const float fromAngle = isBipolar (slider) ? (startAngle + endAngle) * 0.5f : startAngle;
        if (std::abs (valueAngle - fromAngle) > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 juce::jmin (fromAngle, valueAngle), juce::jmax (fromAngle, valueAngle), true);
            g.setColour (colour);
            g.strokePath (value, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Knob body and pointer (or a filmstrip frame inside the ring).
        const float bodyRadius = arcRadius - lineWidth * 1.4f;
        if (skin.knob.isValid())
        {
            const int frame = juce::jlimit (0, skin.knobFrames - 1, juce::roundToInt (position * (float) (skin.knobFrames - 1)));
            const int size = skin.knob.getWidth();
            const auto dest = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre).toNearestInt();
            g.drawImage (skin.knob, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                         0, frame * size, size, size);
            return;
        }
        g.setColour (Palette::panelRaised);
        g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
        g.setColour (Palette::outline);
        g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);
        const auto tip = centre.getPointOnCircumference (bodyRadius * 0.82f, valueAngle);
        const auto base = centre.getPointOnCircumference (bodyRadius * 0.25f, valueAngle);
        g.setColour (Palette::text);
        g.drawLine ({ base, tip }, juce::jmax (1.5f, lineWidth * 0.6f));
    }

    void EurekaLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float, float, juce::Slider::SliderStyle style,
                                              juce::Slider& slider)
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
            return;
        }
        const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
        const auto bar = area.withSizeKeepingCentre (area.getWidth(), 6.0f);
        g.setColour (Palette::track);
        g.fillRoundedRectangle (bar, 3.0f);

        const float from = isBipolar (slider)
                               ? (float) slider.valueToProportionOfLength (0.0) * area.getWidth() + area.getX()
                               : area.getX();
        const auto fill = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, sliderPos), bar.getY(),
                                                                      juce::jmax (from, sliderPos), bar.getBottom());
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (fill, 3.0f);
        if (isBipolar (slider))
        {
            g.setColour (Palette::dimText);
            g.fillRect (juce::Rectangle<float> (from - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f));
        }
        g.setColour (Palette::text);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ sliderPos, bar.getCentreY() }));
    }

    void EurekaLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                          juce::ComboBox& box)
    {
        const auto area = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (box.hasKeyboardFocus (false) ? accentColour.withAlpha (0.6f)
                                                  : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (area, 4.0f, 1.0f);

        juce::Path arrow;
        const float cx = (float) width - 11.0f, cy = (float) height * 0.5f;
        arrow.startNewSubPath (cx - 4.0f, cy - 2.0f);
        arrow.lineTo (cx, cy + 2.0f);
        arrow.lineTo (cx + 4.0f, cy - 2.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.9f : 0.3f));
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font EurekaLookAndFeel::getComboBoxFont (juce::ComboBox&)
    {
        return juce::Font (juce::FontOptions (13.0f));
    }

    void EurekaLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Label* EurekaLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (juce::Font (juce::FontOptions (12.0f)));
        label->setColour (juce::Label::textColourId, Palette::text);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return label;
    }

    void EurekaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                  bool isMouseOverButton, bool)
    {
        const auto area = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto on = button.findColour (juce::TextButton::buttonOnColourId);
        if (button.getToggleState())
        {
            g.setColour (on);
            g.fillRoundedRectangle (area, 5.0f);
        }
        else
        {
            g.setColour (isMouseOverButton ? Palette::panelRaised.brighter (0.08f) : Palette::panelRaised);
            g.fillRoundedRectangle (area, 5.0f);
            g.setColour (Palette::outline);
            g.drawRoundedRectangle (area, 5.0f, 1.0f);
        }
    }

    void EurekaLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
    {
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (button.getToggleState() ? Palette::background
                                             : button.findColour (juce::TextButton::textColourOffId));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0),
                          juce::Justification::centred, 1);
    }

    juce::Font EurekaLookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return juce::Font (juce::FontOptions (13.0f, juce::Font::bold));
    }
}
