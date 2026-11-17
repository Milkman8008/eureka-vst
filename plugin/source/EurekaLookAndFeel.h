#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace ambika_plugin
{
    namespace Palette
    {
        enum class Theme { Dark, Light };
        // Switches every colour below; repaint (or rebuild) the UI afterwards.
        void setTheme (Theme);
        Theme theme();

        extern juce::Colour background;
        extern juce::Colour panel;
        extern juce::Colour panelRaised;
        extern juce::Colour outline;
        extern juce::Colour track;
        extern juce::Colour text;
        extern juce::Colour dimText;
        extern juce::Colour faintText;

        // Brushed metal (silver in light mode, gunmetal in dark mode) for
        // the header, buttons, combo boxes and knob caps.
        juce::ColourGradient metal (juce::Rectangle<float> area, bool pressed = false);
        void fillMetal (juce::Graphics&, juce::Rectangle<float> area, float corner, bool pressed = false,
                        float opacity = 1.0f);

        // One colour per part; the editor's accent follows the edited part.
        juce::Colour part (int index);
        // User highlight colours (from the header's colour button); they win
        // over the theme and skin colours. An empty optional clears one.
        void setPartOverride (int index, std::optional<juce::Colour>);
        std::optional<juce::Colour> partOverride (int index);
    }

    // The UI typeface (Bageo, compiled in), as FontOptions for every text.
    juce::Typeface::Ptr eurekaTypeface();
    juce::FontOptions eurekaFont (float height, int styleFlags = juce::Font::plain);

    // The eureka! logo (compiled in); inverted for the light theme.
    juce::Image eurekaLogo();

    class EurekaLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        EurekaLookAndFeel();
        // Re-reads the palette (after Palette::setTheme).
        void applyTheme();

        void setAccent (juce::Colour colour);
        juce::Colour accent() const { return accentColour; }

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                               juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                           int buttonW, int buttonH, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool isMouseOverButton, bool isButtonDown) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getPopupMenuFont() override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    private:
        juce::Colour accentColour;
    };

    // True when a slider's range is centred on zero (drawn from the middle).
    bool isBipolar (const juce::Slider&);
}
