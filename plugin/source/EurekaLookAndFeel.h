#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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

        // One colour per part; the editor's accent follows the edited part.
        juce::Colour part (int index);
    }

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

    private:
        juce::Colour accentColour;
    };

    // True when a slider's range is centred on zero (drawn from the middle).
    bool isBipolar (const juce::Slider&);
}
