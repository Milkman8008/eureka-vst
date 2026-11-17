#include "EurekaLookAndFeel.h"

#include <array>

#include "Skin.h"
#include "EurekaFonts.h"

namespace ambika_plugin
{
    namespace Palette
    {
        juce::Colour background, panel, panelRaised, outline, track, text, dimText, faintText;

        namespace
        {
            Theme currentTheme = Theme::Dark;
            std::array<std::optional<juce::Colour>, 6> overrides;

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
                // Graphite and gunmetal, with cream lettering: the inverse of
                // the light theme.
                background = juce::Colour (0xff161618);
                panel = juce::Colour (0xff222225);
                panelRaised = juce::Colour (0xff3a3b3f);
                outline = juce::Colour (0xff47484c);
                track = juce::Colour (0xff36373b);
                text = juce::Colour (0xfff3ead6);
                dimText = juce::Colour (0xffb3a88f);
                faintText = juce::Colour (0xff7a7262);
            }
            else
            {
                // Cream white panels with brushed silver hardware.
                background = juce::Colour (0xffece4d2);
                panel = juce::Colour (0xfff9f4e8);
                panelRaised = juce::Colour (0xffe3dfd5);
                outline = juce::Colour (0xffc9c0ae);
                track = juce::Colour (0xffdcd3c1);
                text = juce::Colour (0xff2b2721);
                dimText = juce::Colour (0xff6e6659);
                faintText = juce::Colour (0xffa59c8b);
            }
        }

        Theme theme() { return currentTheme; }

        juce::ColourGradient metal (juce::Rectangle<float> area, bool pressed)
        {
            // Brushed metal: a bright top, a soft horizon in the middle, a
            // darker bottom. Pressed metal is lit from below.
            const bool dark = currentTheme == Theme::Dark;
            const juce::Colour hi (dark ? 0xff6b6c70 : 0xfffdfcf8);
            const juce::Colour mid (dark ? 0xff4a4b4f : 0xffe6e2d9);
            const juce::Colour horizon (dark ? 0xff3d3e42 : 0xffd6d1c6);
            const juce::Colour lo (dark ? 0xff2a2b2e : 0xffc2bcaf);
            juce::ColourGradient gradient (pressed ? lo : hi, 0.0f, area.getY(),
                                           pressed ? hi : lo, 0.0f, area.getBottom(), false);
            gradient.addColour (0.48, pressed ? horizon : mid);
            gradient.addColour (0.52, pressed ? mid : horizon);
            return gradient;
        }

        void fillMetal (juce::Graphics& g, juce::Rectangle<float> area, float corner, bool pressed, float opacity)
        {
            g.setGradientFill (metal (area, pressed));
            g.setOpacity (opacity);
            g.fillRoundedRectangle (area, corner);
            // Fine brushing lines.
            {
                juce::Graphics::ScopedSaveState save (g);
                juce::Path clip;
                clip.addRoundedRectangle (area, corner);
                g.reduceClipRegion (clip);
                const auto line = (currentTheme == Theme::Dark ? juce::Colours::white : juce::Colours::black)
                                      .withAlpha (0.025f * opacity);
                g.setColour (line);
                for (float y = area.getY() + 1.0f; y < area.getBottom(); y += 2.0f)
                    g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
            }
            // Top edge catches the light, the bottom edge falls into shadow.
            g.setColour (juce::Colours::white.withAlpha ((currentTheme == Theme::Dark ? 0.18f : 0.8f) * opacity));
            g.drawHorizontalLine ((int) area.getY() + 1, area.getX() + corner, area.getRight() - corner);
            g.setColour (outline.withMultipliedAlpha (opacity));
            g.drawRoundedRectangle (area, corner, 1.0f);
        }

        void setPartOverride (int index, std::optional<juce::Colour> colour)
        {
            overrides[(size_t) juce::jlimit (0, 5, index)] = colour;
        }

        std::optional<juce::Colour> partOverride (int index)
        {
            return overrides[(size_t) juce::jlimit (0, 5, index)];
        }

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
            if (overrides[i])
                return *overrides[i];
            if (const auto& custom = currentSkin().settings.partColours)
                return (*custom)[i];
            return currentTheme == Theme::Dark ? dark[i] : light[i];
        }
    }

    juce::Typeface::Ptr eurekaTypeface()
    {
        static const auto typeface = juce::Typeface::createSystemTypefaceFor (
            EurekaFonts::Bageo_ttf, (size_t) EurekaFonts::Bageo_ttfSize);
        return typeface;
    }

    juce::FontOptions eurekaFont (float height, int styleFlags)
    {
        return juce::FontOptions (height, styleFlags).withTypeface (eurekaTypeface());
    }

    juce::Image eurekaLogo()
    {
        static const auto original = juce::ImageCache::getFromMemory (EurekaFonts::eurekalogo_jpg,
                                                                      EurekaFonts::eurekalogo_jpgSize);
        static const auto inverted = [&]
        {
            auto image = original.createCopy();
            juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                {
                    const auto c = pixels.getPixelColour (x, y);
                    // Inverted, then tinted cream so white reads as the panel colour.
                    pixels.setPixelColour (x, y, juce::Colour ((uint8_t) ((255 - c.getRed()) * 249 / 255),
                                                               (uint8_t) ((255 - c.getGreen()) * 244 / 255),
                                                               (uint8_t) ((255 - c.getBlue()) * 232 / 255)));
                }
            return image;
        }();
        return Palette::theme() == Palette::Theme::Dark ? original : inverted;
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
        const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
        const bool dark = Palette::theme() == Palette::Theme::Dark;
        // Drop shadow, then a turned-metal cap lit from the top left.
        g.setColour (juce::Colours::black.withAlpha (dark ? 0.45f : 0.18f));
        g.fillEllipse (body.translated (0.0f, 1.5f).expanded (0.5f));
        {
            juce::ColourGradient cap (juce::Colour (dark ? 0xff76777b : 0xffffffff), body.getX(), body.getY(),
                                      juce::Colour (dark ? 0xff26272a : 0xffb9b3a6), body.getRight(), body.getBottom(), false);
            cap.addColour (0.5, juce::Colour (dark ? 0xff45464a : 0xffe2ded4));
            g.setGradientFill (cap);
            g.fillEllipse (body);
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (dark ? 0.16f : 0.55f),
                                        centre.x - bodyRadius * 0.35f, centre.y - bodyRadius * 0.45f,
                                        juce::Colours::transparentWhite, centre.x - bodyRadius * 0.35f + bodyRadius * 0.9f,
                                        centre.y - bodyRadius * 0.45f, true);
            g.setGradientFill (sheen);
            g.fillEllipse (body.reduced (bodyRadius * 0.12f));
        }
        g.setColour (Palette::outline.darker (dark ? 0.3f : 0.1f));
        g.drawEllipse (body, 1.0f);
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
        const auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, bar.getCentreY() });
        g.setGradientFill (Palette::metal (thumb, false));
        g.fillEllipse (thumb);
        g.setColour (Palette::outline.darker (0.2f));
        g.drawEllipse (thumb, 1.0f);
    }

    void EurekaLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                          juce::ComboBox& box)
    {
        const auto area = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
        Palette::fillMetal (g, area, 4.0f, false, box.isEnabled() ? 1.0f : 0.5f);
        if (box.hasKeyboardFocus (false))
        {
            g.setColour (accentColour.withAlpha (0.6f));
            g.drawRoundedRectangle (area, 4.0f, 1.0f);
        }

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
        return juce::Font (eurekaFont (13.0f));
    }

    void EurekaLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Label* EurekaLookAndFeel::createSliderTextBox (juce::Slider& slider)
    {
        auto* label = LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (juce::Font (eurekaFont (12.0f)));
        label->setColour (juce::Label::textColourId, Palette::text);
        label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        return label;
    }

    void EurekaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                  bool isMouseOverButton, bool isButtonDown)
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
            Palette::fillMetal (g, area, 5.0f, isButtonDown, button.isEnabled() ? 1.0f : 0.5f);
            if (isMouseOverButton && ! isButtonDown)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRoundedRectangle (area, 5.0f);
            }
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

    juce::Font EurekaLookAndFeel::getPopupMenuFont() { return juce::Font (eurekaFont (14.0f)); }
    juce::Font EurekaLookAndFeel::getLabelFont (juce::Label& label)
    {
        return juce::Font (eurekaFont (label.getFont().getHeight()));
    }

    // Anything that still asks for the default sans-serif gets Bageo too.
    juce::Typeface::Ptr EurekaLookAndFeel::getTypefaceForFont (const juce::Font& font)
    {
        if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return eurekaTypeface();
        return LookAndFeel_V4::getTypefaceForFont (font);
    }

    juce::Font EurekaLookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return juce::Font (eurekaFont (13.0f, juce::Font::bold));
    }
}
