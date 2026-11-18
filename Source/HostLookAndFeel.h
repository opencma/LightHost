#ifndef LIGHTHOST_HOST_LOOK_AND_FEEL_H
#define LIGHTHOST_HOST_LOOK_AND_FEEL_H

#include "HostDesign.h"

// One theme for host windows, menus, dialogs, and scrolling containers.
class LightHostLookAndFeel : public LookAndFeel_V3
{
public:
    LightHostLookAndFeel()
    {
#if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Segoe UI");
#endif
        refreshPalette();
    }

    void refreshPalette()
    {
        setColour (Label::textColourId, HostDesign::text);
        setColour (ComboBox::textColourId, HostDesign::text);
        setColour (ListBox::outlineColourId, Colours::transparentBlack);
        setColour (ResizableWindow::backgroundColourId, HostDesign::canvas);
        setColour (DocumentWindow::backgroundColourId, HostDesign::canvas);
        setColour (TextEditor::backgroundColourId, HostDesign::control);
        setColour (TextEditor::textColourId, HostDesign::text);
        setColour (TextEditor::highlightColourId, HostDesign::selection);
        setColour (TextEditor::highlightedTextColourId, HostDesign::text);
        setColour (TextEditor::outlineColourId, HostDesign::border);
        setColour (TextEditor::focusedOutlineColourId, HostDesign::accent);
        setColour (CaretComponent::caretColourId, HostDesign::text);
        setColour (TextButton::buttonColourId, HostDesign::control);
        setColour (TextButton::buttonOnColourId, HostDesign::accent);
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (TextButton::textColourOffId, HostDesign::text);
        setColour (ComboBox::backgroundColourId, HostDesign::control);
        setColour (ComboBox::outlineColourId, HostDesign::border);
        setColour (ComboBox::arrowColourId, HostDesign::text);
        setColour (PopupMenu::backgroundColourId, Colours::transparentBlack);
        setColour (PopupMenu::textColourId, HostDesign::text);
        setColour (PopupMenu::headerTextColourId, HostDesign::secondary);
        setColour (PopupMenu::highlightedBackgroundColourId, HostDesign::selection);
        setColour (PopupMenu::highlightedTextColourId, HostDesign::text);
        setColour (ToggleButton::tickColourId, Colour (0xffFFFFFF));
        setColour (ToggleButton::textColourId, HostDesign::text);
        setColour (ListBox::backgroundColourId, HostDesign::canvas);
        setColour (ListBox::textColourId, HostDesign::text);
        setColour (ScrollBar::thumbColourId, HostDesign::palette().scrollThumb);
        setColour (ScrollBar::backgroundColourId, Colours::transparentBlack);
        setColour (AlertWindow::backgroundColourId, HostDesign::canvas);
        setColour (AlertWindow::textColourId, HostDesign::text);
        setColour (AlertWindow::outlineColourId, HostDesign::border);
        setColour (ProgressBar::backgroundColourId, HostDesign::sidebar);
        setColour (ProgressBar::foregroundColourId, HostDesign::accent);
        setColour (HyperlinkButton::textColourId, HostDesign::accent);
        setColour (Slider::backgroundColourId, HostDesign::surface);
        setColour (Slider::thumbColourId, HostDesign::accent);
        setColour (Slider::trackColourId, HostDesign::accent);
        setColour (Slider::textBoxTextColourId, HostDesign::text);
        setColour (Slider::textBoxBackgroundColourId, HostDesign::control);
        setColour (Slider::textBoxOutlineColourId, HostDesign::border);
        setColour (DirectoryContentsDisplayComponent::textColourId, HostDesign::text);
        setColour (DirectoryContentsDisplayComponent::highlightColourId, HostDesign::selection);
    }

    void applyDarkMode(bool dark)
    {
        HostDesign::setDarkMode(dark);
        refreshPalette();
        auto& desktop = Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            desktop.getComponent(i)->sendLookAndFeelChange();
    }

    MouseCursor getMouseCursorFor(Component& component) override
    {
        if (!component.isEnabled()) return MouseCursor::NormalCursor;

        const MouseCursor cursor = LookAndFeel_V3::getMouseCursorFor(component);
        // Keep text insertion, resize, and drag cursors where controls request them.
        if (cursor != MouseCursor::NormalCursor && cursor != MouseCursor::ParentCursor)
            return cursor;

        for (Component* control = &component; control != nullptr; control = control->getParentComponent())
        {
            if (dynamic_cast<TextEditor*>(control) != nullptr) return cursor;
            if (dynamic_cast<Button*>(control) != nullptr || dynamic_cast<ComboBox*>(control) != nullptr
                || dynamic_cast<Slider*>(control) != nullptr || dynamic_cast<ScrollBar*>(control) != nullptr)
                return MouseCursor::PointingHandCursor;

            if (auto* list = dynamic_cast<ListBox*>(control))
            {
                const auto position = list->getMouseXYRelative();
                const int row = list->getRowContainingPosition(position.x, position.y);
                return list->getModel() != nullptr && isPositiveAndBelow(row, list->getModel()->getNumRows())
                    ? MouseCursor::PointingHandCursor : cursor;
            }
        }
        return cursor;
    }

    Typeface::Ptr getTypefaceForFont (const Font& font) override
    {
        return LookAndFeel_V3::getTypefaceForFont (HostDesign::resolveUiFont (font));
    }

    Font getTextButtonFont (TextButton&, int) override { return HostDesign::bodyFont (16.0f); }
    Font getComboBoxFont (ComboBox&) override { return HostDesign::bodyFont (16.0f); }
    Font getPopupMenuFont() override { return HostDesign::bodyFont(15.0f); }
    Font getAlertWindowTitleFont() override { return HostDesign::bodyFont(21.0f); }
    Font getAlertWindowMessageFont() override { return HostDesign::resolveUiFont(Font(16.0f)); }
    Font getAlertWindowFont() override { return HostDesign::bodyFont(15.0f); }
    int getAlertWindowButtonHeight() override { return HostDesign::menuRowHeight; }

    void drawPopupMenuBackground (Graphics& g, int width, int height) override
    {
        HostDesign::drawPopupSurface(g, width, height);
    }

    int getMenuWindowFlags() override { return 0; }

    DropShadower* createDropShadowerForComponent (Component* component) override
    {
        // JUCE 4's separate rectangular shadow windows show through rounded corners.
        // Floating surfaces paint their shadow in the same transparent window instead.
        if (component != nullptr && !component->isOpaque())
            return nullptr;
        return LookAndFeel_V3::createDropShadowerForComponent(component);
    }

    void getIdealPopupMenuItemSize (const String& text, bool separator, int standardHeight,
                                   int& width, int& height) override
    {
        width = separator ? 80 : getPopupMenuFont().getStringWidth(text) + 64;
        height = separator ? 9 : jmax(HostDesign::menuRowHeight, standardHeight);
    }

    void drawPopupMenuItem (Graphics& g, const Rectangle<int>& row, bool separator,
                           bool enabled, bool highlighted, bool ticked, bool submenu,
                           const String& text, const String& shortcut, const Drawable* icon,
                           const Colour* textColour) override
    {
        // JUCE's item components span the full window width, including our shadow gutter.
        const auto area = row.reduced(HostDesign::popupShadowMargin, 0);
        if (separator)
        {
            g.setColour(HostDesign::border);
            g.fillRect(area.getX() + 12, area.getCentreY(), jmax(0, area.getWidth() - 24), 1);
            return;
        }
        Colour ink = textColour != nullptr ? *textColour : HostDesign::text;
        if (enabled && highlighted)
        {
            g.setColour(ink == HostDesign::danger ? HostDesign::danger.withAlpha(0.08f) : HostDesign::selection);
            g.fillRoundedRectangle(area.reduced(4, 2).toFloat(), HostDesign::controlRadius - 2);
        }
        if (!enabled) ink = HostDesign::secondary.withAlpha(0.55f);
        g.setColour(ink);
        const float centreY = (float) area.getCentreY();
        if (icon != nullptr)
            icon->drawWithin(g, Rectangle<float>((float) area.getX() + 11, centreY - 8, 16, 16),
                             RectanglePlacement::centred, enabled ? 1.0f : 0.45f);
        else if (ticked)
        {
            Path check;
            check.startNewSubPath((float) area.getX() + 12, centreY);
            check.lineTo((float) area.getX() + 16, centreY + 4);
            check.lineTo((float) area.getX() + 23, centreY - 4);
            g.strokePath(check, PathStrokeType(1.7f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (submenu)
        {
            Path arrow;
            const float x = (float) area.getRight() - 14;
            arrow.startNewSubPath(x - 3, centreY - 4);
            arrow.lineTo(x + 1, centreY);
            arrow.lineTo(x - 3, centreY + 4);
            g.strokePath(arrow, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        auto label = area.withTrimmedLeft(34).withTrimmedRight(submenu ? 30 : 14);
        if (shortcut.isNotEmpty())
        {
            const Font shortcutFont = HostDesign::resolveUiFont(Font(14.0f));
            const int shortcutWidth = shortcutFont.getStringWidth(shortcut) + 16;
            g.setFont(shortcutFont);
            g.drawText(shortcut, label.removeFromRight(shortcutWidth), Justification::centredRight);
        }
        g.setFont(getPopupMenuFont());
        g.drawText(text, label, Justification::centredLeft);
    }

    void drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& row, const String& title) override
    {
        const auto area = row.reduced(HostDesign::popupShadowMargin, 0);
        auto label = area.reduced(14, 0);
        if (title == "Light Host")
        {
            HostDesign::drawSymbol(g, HostDesign::waveform,
                Rectangle<float>((float) label.getX(), (float) area.getCentreY() - 10, 20, 20), HostDesign::text);
            label.removeFromLeft(28);
        }
        g.setColour(HostDesign::text);
        g.setFont(HostDesign::bodyFont(15.0f));
        g.drawText(title, label, Justification::centredLeft);
        g.setColour(HostDesign::border);
        g.fillRect(area.getX() + 12, area.getBottom() - 1, jmax(0, area.getWidth() - 24), 1);
    }

    bool areScrollbarButtonsVisible() override { return false; }
    int getDefaultScrollbarWidth() override { return HostDesign::scrollbarWidth; }
    int getMinimumScrollbarThumbSize (ScrollBar&) override { return 28; }

    void drawScrollbar (Graphics& g, ScrollBar& scrollbar, int x, int y, int width, int height,
                        bool vertical, int thumbStart, int thumbSize, bool hovered, bool pressed) override
    {
        const auto track = Rectangle<float>((float) x, (float) y, (float) width, (float) height);
        if (hovered || pressed)
        {
            g.setColour(HostDesign::sidebar);
            g.fillRoundedRectangle(track, (vertical ? width : height) * 0.5f);
        }
        if (thumbSize <= 0) return;
        const auto thumb = vertical
            ? Rectangle<float>((float) x + 2, (float) thumbStart + 2, (float) width - 4, (float) jmax(1, thumbSize - 4))
            : Rectangle<float>((float) thumbStart + 2, (float) y + 2, (float) jmax(1, thumbSize - 4), (float) height - 4);
        Colour ink = scrollbar.findColour(ScrollBar::thumbColourId);
        if (hovered || pressed) ink = HostDesign::secondary.withAlpha(pressed ? 1.0f : 0.8f);
        g.setColour(ink.withMultipliedAlpha(scrollbar.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle(thumb, (vertical ? thumb.getWidth() : thumb.getHeight()) * 0.5f);
    }

    void fillTextEditorBackground (Graphics& g, int width, int height, TextEditor& editor) override
    {
        // TextEditor marks itself opaque when its fill is opaque; paint the corners too.
        g.fillAll(HostDesign::canvas);
        g.setColour(editor.findColour(TextEditor::backgroundColourId));
        g.fillRoundedRectangle(Rectangle<float>(0, 0, (float) width, (float) height), HostDesign::controlRadius);
    }

    void drawAlertBox (Graphics& g, AlertWindow& alert, const Rectangle<int>& textArea, TextLayout& layout) override
    {
        g.fillAll(HostDesign::canvas);
        g.setColour(HostDesign::border);
        g.drawRect(alert.getLocalBounds(), 1);
        int iconSpace = 0;
        if (alert.getAlertType() != AlertWindow::NoIcon)
        {
            iconSpace = 80; // Matches the space reserved by JUCE's alert layout.
            const Colour ink = alert.getAlertType() == AlertWindow::WarningIcon ? HostDesign::warning : HostDesign::accent;
            const auto icon = Rectangle<float>((float) textArea.getX() + 20, (float) textArea.getY() + 8, 36, 36);
            g.setColour(ink.withAlpha(0.1f));
            g.fillEllipse(icon);
            g.setColour(ink);
            g.setFont(HostDesign::bodyFont(24.0f));
            g.drawText(alert.getAlertType() == AlertWindow::WarningIcon ? "!"
                : alert.getAlertType() == AlertWindow::QuestionIcon ? "?" : "i", icon, Justification::centred);
        }
        layout.draw(g, textArea.withTrimmedLeft(iconSpace).toFloat());
    }

    void drawProgressBar (Graphics& g, ProgressBar& bar, int width, int height,
                          double progress, const String& label) override
    {
        const float trackY = label.isEmpty() ? height * 0.5f - 3 : (float) height - 6;
        const Rectangle<float> track(0, trackY, (float) width, 6);
        g.setColour(bar.findColour(ProgressBar::backgroundColourId));
        g.fillRoundedRectangle(track, 3);
        g.setColour(bar.findColour(ProgressBar::foregroundColourId));
        if (progress >= 0 && progress <= 1)
            g.fillRoundedRectangle(track.withWidth((float) (width * progress)), 3);
        else
        {
            const float phase = (float) (Time::getMillisecondCounter() % 1600) / 1600.0f;
            g.fillRoundedRectangle(track.withX((float) width * 0.7f * (0.5f - 0.5f * std::cos(phase * float_Pi * 2)))
                .withWidth((float) width * 0.3f), 3);
        }
        if (label.isNotEmpty())
        {
            g.setColour(HostDesign::secondary);
            g.setFont(HostDesign::resolveUiFont(Font(14.0f)));
            g.drawText(label, 0, 0, width, jmax(1, height - 8), Justification::centred);
        }
    }

    void drawButtonText (Graphics& g, TextButton& button, bool, bool) override
    {
        g.setColour (button.findColour (button.getToggleState()
            ? TextButton::textColourOnId : TextButton::textColourOffId)
            .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.78f));
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (7, 0),
                         Justification::centred, 1);
    }

    void drawTextEditorOutline (Graphics& g, int width, int height, TextEditor& editor) override
    {
        g.setColour (editor.hasKeyboardFocus (true) ? HostDesign::accent : HostDesign::border);
        g.drawRoundedRectangle (Rectangle<float> (0.5f, 0.5f, (float) width - 1, (float) height - 1), HostDesign::controlRadius, 1);
    }

    void drawTableHeaderBackground (Graphics& g, TableHeaderComponent& header) override
    {
        g.fillAll (HostDesign::sidebar);
        g.setColour (HostDesign::border);
        g.drawHorizontalLine (header.getHeight() - 1, 0.0f, (float) header.getWidth());
    }

    void drawTableHeaderColumn(Graphics& g, const String& name, int, int width, int height,
                               bool hovered, bool pressed, int flags) override
    {
        if (hovered || pressed) g.fillAll(HostDesign::selection);
        auto area = Rectangle<int>(width, height).reduced(8, 0);
        g.setColour(HostDesign::text);
        if ((flags & (TableHeaderComponent::sortedForwards | TableHeaderComponent::sortedBackwards)) != 0)
        {
            auto arrow = area.removeFromRight(16).toFloat().reduced(3, height * 0.35f);
            Path path;
            const bool forwards = (flags & TableHeaderComponent::sortedForwards) != 0;
            path.addTriangle(arrow.getX(), forwards ? arrow.getBottom() : arrow.getY(),
                             arrow.getCentreX(), forwards ? arrow.getY() : arrow.getBottom(),
                             arrow.getRight(), forwards ? arrow.getBottom() : arrow.getY());
            g.fillPath(path);
        }
        g.setFont(HostDesign::bodyFont(15));
        g.drawFittedText(name, area, Justification::centredLeft, 1);
    }

    void drawLevelMeter(Graphics& g, int width, int height, float level) override
    {
        const auto bounds = Rectangle<float>(0, 0, (float) width, (float) height).reduced(0.5f);
        g.setColour(HostDesign::control);
        g.fillRoundedRectangle(bounds, 5);
        g.setColour(HostDesign::border);
        g.drawRoundedRectangle(bounds, 5, 1);
        const int blocks = 7;
        const int lit = roundToInt(jlimit(0.0f, 1.0f, level) * blocks);
        const float blockWidth = jmax(0.0f, (width - 6.0f) / blocks);
        for (int i = 0; i < blocks; ++i)
        {
            g.setColour(i >= lit ? HostDesign::border : i == blocks - 1 ? HostDesign::danger : HostDesign::green);
            g.fillRoundedRectangle(3 + i * blockWidth, 3, blockWidth * 0.75f, jmax(0.0f, height - 6.0f), 1);
        }
    }

    void drawButtonBackground (Graphics& g, Button& button, const Colour& background,
                               bool isMouseOverButton, bool isButtonDown) override
    {
        Colour fill = button.getToggleState() ? HostDesign::accent : background;
        if (isMouseOverButton)
            fill = fill.overlaidWith(HostDesign::accent.withAlpha(0.10f));
        if (isButtonDown)
            fill = fill.overlaidWith(HostDesign::accent.withAlpha(0.18f));

        const Rectangle<float> bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (button.getToggleState() ? HostDesign::accent : HostDesign::border);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        if (button.hasKeyboardFocus (false))
        {
            g.setColour (HostDesign::accent);
            g.drawRoundedRectangle (bounds.reduced (2.0f), 6.0f, 1.0f);
        }
    }

    void drawComboBox (Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       ComboBox& box) override
    {
        const Rectangle<float> bounds (0.5f, 0.5f, (float) width - 1.0f,
                                       (float) height - 1.0f);
        g.setColour (box.findColour (ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (box.hasKeyboardFocus (false) || isButtonDown
            ? HostDesign::accent : box.findColour (ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        const float centreX = (float) buttonX + (float) buttonW * 0.5f;
        const float centreY = (float) buttonY + (float) buttonH * 0.5f;
        Path arrow;
        arrow.startNewSubPath (centreX - 4, centreY - 2);
        arrow.lineTo (centreX, centreY + 2);
        arrow.lineTo (centreX + 4, centreY - 2);
        g.setColour (box.findColour (ComboBox::arrowColourId));
        g.strokePath (arrow, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    void drawTickBox (Graphics& g, Component&,
                      float x, float y, float width, float height,
                      bool ticked, bool isEnabled, bool isMouseOverButton,
                      bool isButtonDown) override
    {
        const float side = jmin (14.0f, jmin (width, height));
        const Rectangle<float> box (x, y + (height - side) * 0.5f, side, side);
        const Colour accent = HostDesign::accent;

        g.setColour (ticked ? accent : HostDesign::control);
        g.fillRoundedRectangle (box.reduced (0.5f), 3.0f);
        g.setColour (ticked || isMouseOverButton || isButtonDown
            ? accent : HostDesign::secondary);
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.2f);

        if (ticked)
        {
            Path check;
            check.startNewSubPath (box.getX() + side * 0.22f, box.getY() + side * 0.52f);
            check.lineTo (box.getX() + side * 0.43f, box.getY() + side * 0.72f);
            check.lineTo (box.getX() + side * 0.79f, box.getY() + side * 0.30f);
            g.setColour (Colours::white);
            g.strokePath (check, PathStrokeType (2.0f, PathStrokeType::curved,
                                                 PathStrokeType::rounded));
        }

        if (! isEnabled)
        {
            g.setColour (HostDesign::canvas.withAlpha (0.45f));
            g.fillRoundedRectangle (box, 3.0f);
        }
    }
};

#endif
