//
//  IconMenu.hpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#ifndef IconMenu_hpp
#define IconMenu_hpp

ApplicationProperties& getAppProperties();

class IconMenu : public SystemTrayIconComponent, private Timer, public ChangeListener
{
public:
    IconMenu();
    ~IconMenu();
    void mouseDown(const MouseEvent&);
    static void menuInvocationCallback(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed);
	static String getKey(String type, PluginDescription plugin);

	const int INDEX_EDIT, INDEX_BYPASS, INDEX_DELETE, INDEX_MOVE_UP, INDEX_MOVE_DOWN;
private:
    struct ActivePlugin : PluginDescription
    {
        String instanceId;
        uint32 channelMask = 0xff;
    };
    void timerCallback();
    void reloadPlugins();
    void showAudioSettings();
    void loadActivePlugins();
    bool savePluginStates();
    void deletePluginStates();
	void removePluginsLackingInputOutput();
    std::vector<ActivePlugin> getTimeSortedList();
    static String getKey (String type, const ActivePlugin& plugin);
    static String getInstanceStateKey (const ActivePlugin& plugin, int channel, bool independentChannels);
    static AudioProcessorGraph::NodeID getPluginNodeId (int pluginIndex, int channel, bool independentChannels);
    bool usesIndependentChannelRouting() const;
    void setIndependentChannelRouting (bool enabled);
    String getAudioDeviceFingerprint() const;
    void recoverAudioDevice();
    void editPluginChannels (ActivePlugin plugin);
    void restoreActivePlugins();
    bool persistActivePlugins (const std::vector<ActivePlugin>& plugins);
	void setIcon();
    void locatePlugin (ActivePlugin plugin);
    void replacePluginWithDefaults (ActivePlugin oldPlugin, PluginDescription replacement);
    void showMainWindow();
    void addPluginFile();
    void checkForUpdates (bool manual);
    void downloadUpdate (String url, String sha256, String version);
    
    AudioDeviceManager deviceManager;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    std::vector<ActivePlugin> activePlugins;
    StringArray pluginLoadErrors;
    KnownPluginList::SortMethod pluginSortMethod;
    PopupMenu menu;
    std::unique_ptr<PluginDirectoryScanner> scanner;
    bool menuIconLeftClicked;
    bool mainWindowAction = false;
    bool activeChainValid = true;
    bool updateInProgress = false;
    bool audioSettingsDialogOpen = false;
    bool recoveringAudioDevice = false;
    String lastAudioDeviceFingerprint;
    int64 lastAudioRecoveryAttempt = 0;
    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
	#if JUCE_WINDOWS
	int x, y;
	#endif

	class PluginListWindow;
    class MainWindow;
    std::unique_ptr<MainWindow> mainWindow;
	std::unique_ptr<PluginListWindow> pluginListWindow;
    std::unique_ptr<FileChooser> pluginFileChooser;
};

#endif /* IconMenu_hpp */
