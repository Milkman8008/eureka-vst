#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ambika_plugin
{
    namespace Palette
    {
        const juce::Colour background { 0xff121419 };
        const juce::Colour panel { 0xff1b1e25 };
        const juce::Colour panelRaised { 0xff232731 };
        const juce::Colour outline { 0xff2e3440 };
        const juce::Colour track { 0xff343b48 };
        const juce::Colour text { 0xffdde1e8 };
        const juce::Colour dimText { 0xff858e9e };
        const juce::Colour faintText { 0xff5b6372 };

        // One colour per part; the editor's accent follows the edited part.
        juce::Colour part (int index);
    }

    class EurekaLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        EurekaLookAndFeel();

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
