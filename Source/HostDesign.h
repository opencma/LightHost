#ifndef LIGHTHOST_HOST_DESIGN_H
#define LIGHTHOST_HOST_DESIGN_H

#include <functional>
#include <cmath>

// Shared by the host's controls. Plugin-provided editors keep their own styling.
namespace HostDesign
{
struct Palette
{
    explicit Palette(bool dark_ = true)
        : dark(dark_),
          canvas(dark ? 0xff161616 : 0xffFFFFFF),
          sidebar(dark ? 0xff1B1B1B : 0xffF5F6F8),
          surface(dark ? 0xff2F2F2F : 0xffFAFBFC),
          control(dark ? 0xff363636 : 0xffFFFFFF),
          text(dark ? 0xffFFFFFF : 0xff202329),
          secondary(dark ? 0xffAEAEAE : 0xff4E5663),
          border(dark ? 0xff3C3C3C : 0xffDEE2E8),
          accent(dark ? 0xff0A84FF : 0xff0078D4),
          selection(dark ? 0xff183D63 : 0xffEAF3FC),
          green(dark ? 0xff32D74B : 0xff278650),
          danger(dark ? 0xffFF6961 : 0xffC42B32),
          warning(dark ? 0xffFFB340 : 0xffAC6500),
          switchOff(dark ? 0xff636363 : 0xffD6DBE2),
          scrollThumb(dark ? 0xff747474 : 0xffA4ACB7) {}

    bool dark;
    Colour canvas, sidebar, surface, control, text, secondary, border, accent,
           selection, green, danger, warning, switchOff, scrollThumb;
};

// One palette across translation units; references stay valid when appearance changes.
inline Palette& palette() { static Palette value; return value; }
inline void setDarkMode(bool dark) { palette() = Palette(dark); }
inline bool isDarkMode() { return palette().dark; }
static const Colour& canvas = palette().canvas;
static const Colour& sidebar = palette().sidebar;
static const Colour& surface = palette().surface;
static const Colour& control = palette().control;
static const Colour& text = palette().text;
static const Colour& secondary = palette().secondary;
static const Colour& border = palette().border;
static const Colour& accent = palette().accent;
static const Colour& selection = palette().selection;
static const Colour& green = palette().green;
static const Colour& danger = palette().danger;
static const Colour& warning = palette().warning;

inline void updateNativeTitleBar(Component& window)
{
#if JUCE_WINDOWS
    if (auto* peer = window.getPeer())
    {
        DynamicLibrary dwm("dwmapi.dll");
        using SetAttribute = long (__stdcall*)(void*, unsigned long, const void*, unsigned long);
        if (auto setAttribute = reinterpret_cast<SetAttribute>(dwm.getFunction("DwmSetWindowAttribute")))
        {
            const int dark = isDarkMode() ? 1 : 0;
            // Windows 10 before 20H1 uses attribute 19; newer versions use 20.
            if (setAttribute(peer->getNativeHandle(), 20, &dark, sizeof(dark)) < 0)
                setAttribute(peer->getNativeHandle(), 19, &dark, sizeof(dark));
        }
    }
#else
    ignoreUnused(window);
#endif
}
const float controlRadius = 8.0f, popupRadius = 10.0f;
const int menuRowHeight = 34, scrollbarWidth = 10;
// Leave room for the complete rounded shadow inside the transparent native window.
const int popupShadowMargin = 10;

inline Font resolveUiFont(Font font)
{
#if JUCE_WINDOWS
    if (font.getTypefaceName() == Font::getDefaultSansSerifFontName())
        font.setTypefaceName("Segoe UI");
    // JUCE 4 passes its default-style placeholder to DirectWrite unchanged
    // when a default family is overridden. Segoe UI then falls back to Light.
    if (font.getTypefaceStyle() == Font::getDefaultStyle())
        font.setTypefaceStyle("Regular");
#endif
    return font;
}

inline Font bodyFont(float height = 16.0f)
{
    Font font(height);
#if JUCE_WINDOWS
    font.setTypefaceStyle("Semibold");
#endif
    return resolveUiFont(font);
}

inline String effectStatus(bool available, bool bypassed)
{
    if (bypassed) return available ? "Bypassed" : "Bypassed / Unavailable";
    return available ? "Active" : "Unavailable";
}

inline Colour powerColour(bool available, bool bypassed)
{
    return bypassed ? secondary : available ? accent : warning;
}

const int effectRowHeight = 68;

inline void drawPopupSurface(Graphics& g, int width, int height)
{
    const auto panel = Rectangle<float>(0, 0, (float) width, (float) height).reduced((float) popupShadowMargin);
    Path path;
    path.addRoundedRectangle(panel, popupRadius);
    DropShadow(Colours::black.withAlpha(isDarkMode() ? 0.35f : 0.13f), 6, Point<int>(0, 2)).drawForPath(g, path);
    g.setColour(isDarkMode() ? surface : canvas);
    g.fillPath(path);
    g.setColour(border);
    g.strokePath(path, PathStrokeType(1));
}

class Switch : public ToggleButton
{
public:
    explicit Switch (const String& text)
        : ToggleButton (text)
    {
        setClickingTogglesState (true);
    }

    void paintButton (Graphics& g, bool isMouseOverButton, bool isButtonDown) override
    {
        const juce::Rectangle<float> buttonBounds = getLocalBounds().toFloat();
        juce::Rectangle<float> track (buttonBounds.getRight() - 42.0f,
                                      buttonBounds.getCentreY() - 10.0f, 38.0f, 20.0f);
        const bool enabled = getToggleState();

        Colour trackColour = enabled ? accent : palette().switchOff;
        if (isMouseOverButton || isButtonDown)
            trackColour = trackColour.brighter (isButtonDown ? 0.12f : 0.06f);
        g.setColour (trackColour.withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
        g.fillRoundedRectangle (track, 10.0f);
        g.setColour (Colours::white);
        const float thumbX = enabled ? track.getRight() - 18.0f : track.getX() + 2.0f;
        g.fillEllipse (thumbX, track.getY() + 2.0f, 16.0f, 16.0f);

        g.setColour (HostDesign::text.withAlpha (isEnabled() ? 1.0f : 0.5f));
        g.setFont (HostDesign::bodyFont(16.0f));
        g.drawText (getButtonText(), 0, 0, getWidth() - 52, getHeight(),
                    Justification::centredLeft, true);

        if (hasKeyboardFocus (false))
        {
            g.setColour (accent);
            g.drawRoundedRectangle (buttonBounds.reduced (0.5f), 5.0f, 1.0f);
        }

    }
};

class SwitchMenuItem : public PopupMenu::CustomComponent
{
public:
    SwitchMenuItem(const String& label, bool enabled) : CustomComponent(true), toggle(label)
    {
        setName(label);
        setMouseCursor(MouseCursor::PointingHandCursor);
        toggle.setToggleState(enabled, dontSendNotification);
        // Let the menu handle the whole row, including keyboard selection.
        toggle.setInterceptsMouseClicks(false, false);
        toggle.setWantsKeyboardFocus(false);
        addAndMakeVisible(toggle);
    }

    void getIdealSize(int& width, int& height) override
    {
        width = jmax(240, bodyFont(15).getStringWidth(getName()) + 110);
        height = menuRowHeight;
    }

    void resized() override
    {
        toggle.setBounds(getLocalBounds().reduced(popupShadowMargin, 0)
                            .withTrimmedLeft(34).withTrimmedRight(10));
    }

    void paint(Graphics& g) override
    {
        if (isItemHighlighted())
        {
            g.setColour(selection);
            g.fillRoundedRectangle(getLocalBounds().reduced(popupShadowMargin + 4, 2).toFloat(), controlRadius - 2);
        }
    }

private:
    Switch toggle;
};

enum Symbol { waveform, chain, settings, info, effect, power, more, plus, input, output, search, folder };

inline void drawSymbol (Graphics& g, Symbol symbol, Rectangle<float> bounds, Colour colour)
{
    Graphics::ScopedSaveState state (g);
    g.addTransform (AffineTransform::scale (bounds.getWidth() / 24.0f, bounds.getHeight() / 24.0f)
                        .translated (bounds.getX(), bounds.getY()));
    g.setColour (colour);
    Path p;
    switch (symbol)
    {
        case waveform:
            for (int i = 0; i < 5; ++i)
            {
                const float heights[] = { 6, 14, 22, 14, 6 };
                g.fillRoundedRectangle (2.0f + i * 4.5f, 12.0f - heights[i] / 2,
                                        2.0f, heights[i], 1.0f);
            }
            return;
        case chain:
            p.addRoundedRectangle (2, 11, 13, 8, 4);
            p.addRoundedRectangle (9, 5, 13, 8, 4);
            p.applyTransform (AffineTransform::rotation (-0.55f, 12, 12));
            break;
        case settings:
            for (int i = 0; i < 3; ++i)
            {
                const float y = 6.0f + i * 6.0f;
                const float x = i == 1 ? 16.0f : 8.0f;
                p.startNewSubPath (3, y); p.lineTo (x - 3, y);
                p.addEllipse (x - 3, y - 3, 6, 6);
                p.startNewSubPath (x + 3, y); p.lineTo (21, y);
            }
            break;
        case info:
            p.addEllipse (2, 2, 20, 20);
            p.startNewSubPath (12, 11); p.lineTo (12, 17);
            g.fillEllipse (11, 6, 2, 2);
            break;
        case effect:
            for (int i = 0; i < 3; ++i)
            {
                float x = 5.0f + i * 7.0f;
                float y = i == 1 ? 8.0f : 15.0f;
                p.startNewSubPath (x, 3); p.lineTo (x, y - 2);
                p.startNewSubPath (x, y + 2); p.lineTo (x, 21);
                p.addRoundedRectangle (x - 2, y - 2, 4, 4, 1);
            }
            break;
        case power:
            p.addCentredArc (12, 13, 8, 8, 0, 0.65f, float_Pi * 2 - 0.65f, true);
            p.startNewSubPath (12, 2); p.lineTo (12, 12);
            break;
        case more:
            for (int i = 0; i < 3; ++i) g.fillEllipse (4.0f + i * 7, 10.5f, 3, 3);
            return;
        case plus:
            p.startNewSubPath (5, 12); p.lineTo (19, 12);
            p.startNewSubPath (12, 5); p.lineTo (12, 19);
            break;
        case input:
        case output:
            p.startNewSubPath (14, 3); p.lineTo (21, 3); p.lineTo (21, 21); p.lineTo (14, 21);
            p.startNewSubPath (2, 12); p.lineTo (16, 12);
            p.startNewSubPath (11, 7); p.lineTo (16, 12); p.lineTo (11, 17);
            if (symbol == output) p.applyTransform (AffineTransform::rotation (float_Pi, 12, 12));
            break;
        case search:
            p.addEllipse (3, 3, 13, 13);
            p.startNewSubPath (15, 15); p.lineTo (21, 21);
            break;
        case folder:
            p.startNewSubPath (2, 7); p.lineTo (2, 4); p.lineTo (9, 4); p.lineTo (12, 7);
            p.lineTo (22, 7); p.lineTo (22, 20); p.lineTo (2, 20); p.closeSubPath();
            p.startNewSubPath (2, 9); p.lineTo (22, 9);
            break;
    }
    g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

inline void drawPowerButton(Graphics& g, Rectangle<float> bounds, bool available, bool bypassed, bool hovered)
{
    const bool darkBypass = isDarkMode() && bypassed;
    Colour fill = darkBypass ? control : powerColour(available, bypassed);
    if (darkBypass && hovered) fill = fill.overlaidWith(accent.withAlpha(0.10f));
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 7);
    if (darkBypass)
    {
        g.setColour(border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 7, 1);
    }
    drawSymbol(g, power, bounds.reduced(6), darkBypass ? text : Colours::white);
}

inline void drawPanel (Graphics& g, Rectangle<int> bounds)
{
    g.setColour (canvas);
    g.fillRoundedRectangle (bounds.toFloat(), 10.0f);
    g.setColour (border);
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 10.0f, 1.0f);
}

class NavigationButton : public Button
{
public:
    NavigationButton (const String& name, Symbol symbol_) : Button (name), symbol (symbol_)
    {
        setButtonText (name);
        setClickingTogglesState (true);
    }

    void paintButton (Graphics& g, bool hovered, bool pressed) override
    {
        if (getToggleState() || hovered || pressed)
        {
            g.setColour (getToggleState() ? selection : border.withAlpha(0.45f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1), 7);
        }
        Colour ink = getToggleState() ? (isDarkMode() ? HostDesign::text : accent.darker (0.2f)) : HostDesign::text;
        drawSymbol (g, symbol, Rectangle<float> (13, getHeight() / 2.0f - 10, 20, 20), ink);
        g.setColour (ink);
        g.setFont (getToggleState() ? Font (16.0f, Font::bold) : bodyFont(16.0f));
        g.drawText (getButtonText(), 45, 0, getWidth() - 53, getHeight(), Justification::centredLeft);
        if (hasKeyboardFocus (false))
        {
            g.setColour (accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2), 6, 1);
        }
    }
private:
    Symbol symbol;
};

class ActionListBox : public ListBox
{
public:
    ActionListBox (const String& name, ListBoxModel* model) : ListBox (name, model) {}
    std::function<bool(const KeyPress&)> onKeyPress;
    bool keyPressed (const KeyPress& key) override
    {
        return (onKeyPress && onKeyPress (key)) || ListBox::keyPressed (key);
    }
};
}
#endif
