// The module's user interface logic: what the knobs, buttons and Select encoder
// do, and what the 128x128 colour display shows. Kept apart from the JUCE editor
// so it can be driven and rendered without a window (tests, screenshots).
#pragma once
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace ecr
{
class EcrProcessor;

enum class Knob { Width, Xfade, Predelay, Pos, Dry, Wet, Attack, Release };

class Ui
{
public:
    explicit Ui (EcrProcessor&);

    // knobs: the parameter under a knob depends on shift and on the foreground convolver
    juce::RangedAudioParameter* paramFor (Knob, bool shift) const;
    void knobMoved (Knob, bool shift);
    void cvMoved (int which);                       // 0 pos, 1 x, 2 width, 3 aux

    // buttons
    void pressShift();
    void pressRecord();
    void pressStore (bool shift);
    void pressPlay (bool shift);
    bool shiftLatched() const { return shiftLatch; }
    void shiftUsed();                               // a shifted action finished

    // Select encoder
    void turnSelect (int steps, bool shift);
    void pushSelect (bool shift);
    void holdSelect();

    void tick();                                    // call ~30 times a second
    void wake();                                    // any front-panel activity
    void render (juce::Image& display);             // 128x128 ARGB
    void stateRestored();

    // button lights
    bool shiftLed() const { return shiftLatch; }
    bool recordLed() const;
    bool storeLed() const;
    bool playLed() const;

    enum Screen { Main, Chooser, Menu };
    Screen screen = Main;
    static constexpr int kNumMenuItems = 9;

    void setMessage (const juce::String& m) { message = m; }       // top line of the main screen

    // state for editors that draw the screen their own way
    const juce::String& currentMessage() const { return message; }
    juce::String currentPopup() const { return juce::Time::getMillisecondCounter() < popupUntil ? popupText : juce::String(); }
    int chooserIndex() const { return chooserSel; }
    int menuIndex() const { return menuSel; }
    void menuParts (int item, juce::String& label, juce::String& value) const
    {
        const auto line = menuLine (item);
        label = line.upToLastOccurrenceOf (" ", false, false).trim();
        value = line.fromLastOccurrenceOf (" ", false, false);
        if (label.isEmpty()) { label = value; value.clear(); }
    }

private:
    void popup (const juce::String& text, int ms);
    void renderMain (juce::Image&);
    void renderChooser (juce::Image&);
    void renderMenu (juce::Image&);
    juce::String menuLine (int item) const;
    void activateMenuItem (int item);

    EcrProcessor& proc;
    juce::String message { "Ready" };
    juce::String popupText;
    juce::uint32 popupUntil = 0, lastActivity = 0, chooserTouched = 0, storeFlashUntil = 0;
    bool shiftLatch = false;
    int chooserSel = 0, menuSel = 0;
};
} // namespace ecr
