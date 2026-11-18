#ifndef PluginWindow_h
#define PluginWindow_h

class PluginWindow  : public DocumentWindow
{
public:
    enum WindowFormatType
    {
        Normal = 0,
        Generic
    };

    PluginWindow (Component* pluginEditor, AudioProcessorGraph::Node*, WindowFormatType);
    ~PluginWindow();

    static PluginWindow* getWindowFor (AudioProcessorGraph::Node*, WindowFormatType);

    static void closeAllCurrentlyOpenWindows();
    static void closeWindowsFor(AudioProcessorGraph::Node*);
    static void setTitleFor(AudioProcessorGraph::Node*, const String&);
    static bool containsActiveWindows();

    void moved() override;
    void closeButtonPressed() override;
    void lookAndFeelChanged() override;

private:
    AudioProcessorGraph::Node* owner;
    WindowFormatType type;
    const Identifier lastXProp, lastYProp;

    float getDesktopScaleFactor() const override     { return 1.0f; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

inline String toString (PluginWindow::WindowFormatType type)
{
    switch (type)
    {
        case PluginWindow::Normal:     return "Normal";
        case PluginWindow::Generic:    return "Generic";
        default:                       return String();
    }
}

inline String getLastXProp (PluginWindow::WindowFormatType type)    { return "uiLastX_" + toString (type); }
inline String getLastYProp (PluginWindow::WindowFormatType type)    { return "uiLastY_" + toString (type); }


#endif /* PluginWindow_hpp */
