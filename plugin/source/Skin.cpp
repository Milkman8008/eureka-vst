#include "Skin.h"

#if EUREKA_HAS_BUILTIN_SKIN
 #include "EurekaSkinData.h"
#endif

namespace ambika_plugin
{
    namespace
    {
        const char* const kPageNames[] = { "synth", "mod", "multi", "presets", "seq" };

        // Reads a skin file from the user folder, else from the built-in skin.
        juce::MemoryBlock readSkinFile (const juce::String& name, juce::StringArray& sources)
        {
            const auto file = Skin::userDirectory().getChildFile (name);
            if (file.existsAsFile())
            {
                juce::MemoryBlock data;
                if (file.loadFileAsData (data))
                {
                    sources.addIfNotAlreadyThere ("Documents/eureka/skin");
                    return data;
                }
            }
           #if EUREKA_HAS_BUILTIN_SKIN
            for (int i = 0; i < EurekaSkinData::namedResourceListSize; ++i)
            {
                if (juce::String (EurekaSkinData::originalFilenames[i]) == name)
                {
                    int size = 0;
                    if (const auto* bytes = EurekaSkinData::getNamedResource (EurekaSkinData::namedResourceList[i], size))
                    {
                        sources.addIfNotAlreadyThere ("built in");
                        return juce::MemoryBlock (bytes, (size_t) size);
                    }
                }
            }
           #endif
            return {};
        }

        juce::Image readImage (const juce::String& name, juce::StringArray& sources)
        {
            const auto data = readSkinFile (name, sources);
            return data.isEmpty() ? juce::Image() : juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
        }

        Skin& storage()
        {
            static Skin skin;
            return skin;
        }
    }

    juce::File Skin::userDirectory()
    {
        // Override for testing and for keeping skins elsewhere.
        if (const auto custom = juce::SystemStats::getEnvironmentVariable ("EUREKA_SKIN_DIR", {}); custom.isNotEmpty())
            return juce::File (custom);
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("eureka").getChildFile ("skin");
    }

    Skin Skin::load()
    {
        Skin skin;
        juce::StringArray sources;
        skin.background = readImage ("background.png", sources);
        for (size_t i = 0; i < skin.pageBackgrounds.size(); ++i)
            skin.pageBackgrounds[i] = readImage ("background-" + juce::String (kPageNames[i]) + ".png", sources);
        skin.logo = readImage ("logo.png", sources);
        skin.knob = readImage ("knob.png", sources);
        if (skin.knob.isValid())
        {
            const int width = skin.knob.getWidth();
            skin.knobFrames = width > 0 ? skin.knob.getHeight() / width : 0;
            if (skin.knobFrames < 2)
            {
                skin.knob = {};  // Not a filmstrip of square frames.
                skin.knobFrames = 0;
            }
        }

        const auto json = readSkinFile ("skin.json", sources);
        if (! json.isEmpty())
        {
            const auto settings = juce::JSON::parse (json.toString());
            auto number = [&settings] (const char* key, float fallback)
            {
                const auto v = settings.getProperty (key, {});
                return v.isVoid() ? fallback : juce::jlimit (0.0f, 1.0f, (float) v);
            };
            skin.settings.panelOpacity = number ("panelOpacity", 1.0f);
            skin.settings.headerOpacity = number ("headerOpacity", 1.0f);
            skin.settings.displayOpacity = number ("displayOpacity", 1.0f);
            skin.settings.knobValueArc = (bool) settings.getProperty ("knobValueArc", true);
            if (auto* colours = settings.getProperty ("partColours", {}).getArray(); colours != nullptr && colours->size() == 6)
            {
                std::array<juce::Colour, 6> parsed;
                for (int i = 0; i < 6; ++i)
                    parsed[(size_t) i] = juce::Colour::fromString ("ff" + (*colours)[i].toString().trimCharactersAtStart ("#"));
                skin.settings.partColours = parsed;
            }
        }
        skin.source = sources.joinIntoString (" + ");
        return skin;
    }

    const juce::Image& Skin::backgroundFor (int page) const
    {
        if (page >= 0 && page < (int) pageBackgrounds.size() && pageBackgrounds[(size_t) page].isValid())
            return pageBackgrounds[(size_t) page];
        return background;
    }

    const Skin& currentSkin() { return storage(); }
    void setCurrentSkin (Skin skin) { storage() = std::move (skin); }
}
