#ifndef LIGHTHOST_PLUGIN_LIBRARY_VIEW_H
#define LIGHTHOST_PLUGIN_LIBRARY_VIEW_H

#include <algorithm>
#include <map>
#include <vector>

class PluginLibraryView : public Component, public FileDragAndDropTarget, private ListBoxModel,
                          private TextEditor::Listener, private ChangeListener, private Button::Listener
{
public:
    PluginLibraryView (KnownPluginList& known_, PluginInstances& active_)
        : known (known_), active (active_), list ("Plugin library", this)
    {
        searchBox.setName ("Search plugins");
        searchBox.setFont (HostDesign::bodyFont(16.0f));
        searchBox.setTextToShowWhenEmpty ("Search plugins", HostDesign::secondary);
        searchBox.setIndents (32, 8);
        searchBox.addListener (this);
        addAndMakeVisible (searchBox);
        list.setRowHeight (88);
        list.setOutlineThickness (0);
        list.setColour (ListBox::backgroundColourId, HostDesign::canvas);
        list.setMultipleSelectionEnabled (false);
        addAndMakeVisible (list);
        empty.setJustificationType (Justification::centred);
        empty.setFont (HostDesign::bodyFont(15.0f));
        empty.setColour (Label::textColourId, HostDesign::secondary);
        empty.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (empty);
        for (auto* button : { &scanButton, &manageButton })
        {
            addAndMakeVisible (button);
            button->addListener (this);
        }
        known.addChangeListener (this);
        active.addChangeListener (this);
        refresh();
    }

    ~PluginLibraryView() override
    {
        known.removeChangeListener (this);
        active.removeChangeListener (this);
        list.setModel (nullptr);
    }

    std::function<void(const PluginDescription&)> onAddPlugin;

    void configureScanning (AudioPluginFormatManager& formats_, PropertiesFile& settings_)
    {
        formats = &formats_;
        settings = &settings_;
    }

    void focusSearch() { searchBox.grabKeyboardFocus(); searchBox.selectAll(); }

    void importPlugins()
    {
        if (formats == nullptr || settings == nullptr) return;

        const File downloads = File::getSpecialLocation(File::userHomeDirectory).getChildFile("Downloads");
        const File defaultDirectory = downloads.isDirectory()
            ? downloads : File::getSpecialLocation(File::userDocumentsDirectory);
        const File rememberedDirectory(settings->getValue("lastPluginImportPath", defaultDirectory.getFullPathName()));
        const File initialDirectory = rememberedDirectory.isDirectory() ? rememberedDirectory : defaultDirectory;
       #if JUCE_WINDOWS
        const String filter = "*.dll;*.vst3";
       #elif JUCE_MAC
        const String filter = "*.vst;*.vst3;*.component";
       #else
        const String filter = "*.so;*.vst3";
       #endif
        FileChooser chooser("Import plugins", initialDirectory, filter, true);
        Component::SafePointer<PluginLibraryView> safe(this);
        const bool selected = chooser.browseForMultipleFilesToOpen();
        if (safe == nullptr || !selected) return;

        const auto& files = chooser.getResults();
        if (files.isEmpty()) return;
        settings->setValue("lastPluginImportPath", files.getReference(0).getParentDirectory().getFullPathName());

        StringArray failed;
        String firstImported;
        for (const auto& file : files)
        {
            OwnedArray<PluginDescription> found;
            StringArray paths;
            paths.add(file.getFullPathName());
            known.scanAndAddDragAndDroppedFiles(*formats, paths, found);
            if (found.isEmpty()) failed.add(file.getFullPathName());
            else if (firstImported.isEmpty()) firstImported = PluginState::key("library", *found[0]);
        }
        if (firstImported.isNotEmpty()) searchBox.clear();
        refresh();
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[i].identity == firstImported)
            {
                list.selectRow(i);
                list.scrollToEnsureRowIsOnscreen(i);
                break;
            }
        if (!failed.isEmpty())
            AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Plugin import",
                "These files could not be loaded as compatible plugins:\n\n" + failed.joinIntoString("\n"));
    }

    void lookAndFeelChanged() override
    {
        list.setColour(ListBox::backgroundColourId, HostDesign::canvas);
        empty.setColour(Label::textColourId, HostDesign::secondary);
        searchBox.setTextToShowWhenEmpty("Search plugins", HostDesign::secondary);
    }

    bool isInterestedInFileDrag (const StringArray&) override { return formats != nullptr; }
    void filesDropped (const StringArray& files, int, int) override
    {
        if (formats == nullptr) return;
        OwnedArray<PluginDescription> found;
        known.scanAndAddDragAndDroppedFiles(*formats, files, found);
        refresh();
    }

    void refresh()
    {
        const int selected = list.getSelectedRow();
        const String selectedKey = isPositiveAndBelow (selected, (int) rows.size())
            ? rows[selected].identity : String();
        rows.clear();
        const String query = searchBox.getText().trim();
        for (int i = 0; i < known.getNumTypes(); ++i)
        {
            const auto& plugin = *known.getType (i);
            if (query.isEmpty() || (plugin.name + " " + plugin.manufacturerName + " "
                + plugin.pluginFormatName + " " + plugin.category + " " + plugin.version
                + " " + plugin.descriptiveName).containsIgnoreCase (query))
                rows.push_back ({ plugin, PluginState::key("library", plugin),
                    plugin.manufacturerName.isEmpty() ? "Unknown maker" : plugin.manufacturerName,
                    metadata(plugin), 0 });
        }
        std::stable_sort (rows.begin(), rows.end(), [this] (const Row& a, const Row& b)
        {
            const auto group = [this] (const PluginDescription& plugin)
            {
                return sortBy == 1 ? plugin.manufacturerName : sortBy == 2 ? plugin.pluginFormatName
                    : sortBy == 3 ? plugin.category : String();
            };
            const int comparison = group(a.plugin).compareIgnoreCase(group(b.plugin));
            if (comparison != 0) return comparison < 0;
            return a.plugin.name.compareIgnoreCase (b.plugin.name) < 0;
        });
        refreshInstanceCounts();
        list.deselectAllRows();
        list.updateContent();
        for (int i = 0; i < (int) rows.size(); ++i)
            if (selectedKey == rows[i].identity) { list.selectRow (i); break; }
        empty.setText (known.getNumTypes() == 0 ? "Your library is ready for effects.\nScan for plugins to get started."
            : "No matching plugins.\nTry a different name or format.", dontSendNotification);
        empty.setVisible (rows.empty());
        list.repaint();
        repaint();
    }

    void refreshInstanceCounts()
    {
        // Chain edits do not change search results or library ordering. Cache
        // counts here so painting a row never scans the chain or encodes keys.
        std::map<String, int> counts;
        for (int i = 0; i < active.getNumTypes(); ++i)
            ++counts[PluginState::key("library", *active.getType(i))];
        for (auto& row : rows)
        {
            const auto found = counts.find(row.identity);
            row.instanceCount = found != counts.end() ? found->second : 0;
        }
        list.repaint();
    }

    void paint (Graphics& g) override
    {
        g.setColour (HostDesign::text);
        g.setFont (Font (21.0f, Font::bold));
        g.drawText ("Plugin library", 0, 0, getWidth() - 55, 28, Justification::centredLeft);
        g.setColour (HostDesign::sidebar);
        g.fillRoundedRectangle ((float) getWidth() - 46, 4, 44, 22, 11);
        g.setColour (HostDesign::secondary);
        g.setFont (HostDesign::bodyFont(15.0f));
        g.drawText (String ((int) rows.size()), getWidth() - 46, 4, 44, 22, Justification::centred);
        g.setFont (Font (12.5f, Font::bold));
        g.drawText ("INSTALLED", 0, 88, getWidth(), 20, Justification::centredLeft);
    }

    void resized() override
    {
        searchBox.setBounds (0, 40, getWidth(), 34);
        list.setBounds (0, 114, getWidth(), jmax (0, getHeight() - 214));
        empty.setBounds (list.getBounds().reduced (10));
        scanButton.setBounds (0, getHeight() - 82, getWidth(), 34);
        manageButton.setBounds (0, getHeight() - 38, getWidth(), 32);
    }

private:
    struct Row
    {
        PluginDescription plugin;
        String identity, manufacturer, details;
        int instanceCount;
    };

    class SearchEditor : public TextEditor
    {
        void paintOverChildren (Graphics& g) override
        {
            TextEditor::paintOverChildren (g);
            HostDesign::drawSymbol (g, HostDesign::search, juce::Rectangle<float> (10, 9, 16, 16), HostDesign::secondary);
        }
    };

    void add (const PluginDescription& plugin)
    {
        // A row owns a description snapshot. A scan, sort, or search must never redirect its action.
        const PluginDescription copy (plugin);
        for (int i = 0; i < known.getNumTypes(); ++i)
            if (PluginState::sameIdentity(copy, *known.getType(i)))
            {
                if (onAddPlugin) onAddPlugin (copy);
                refreshInstanceCounts();
                return;
            }
    }

    class AddControl : public Component, private Button::Listener
    {
    public:
        explicit AddControl (PluginLibraryView& owner_) : owner (owner_)
        {
            setInterceptsMouseClicks (false, true);
            addAndMakeVisible (button);
            button.addListener (this);
        }
        void update (const PluginDescription& plugin_)
        {
            plugin = plugin_;
            button.setButtonText ("+");
            button.setEnabled (true);
        }
        void resized() override { button.setBounds (getWidth() - 54, 14, 52, 32); }
    private:
        void buttonClicked (Button*) override { owner.add (plugin); }
        PluginLibraryView& owner;
        PluginDescription plugin;
        TextButton button;
    };

    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem (int row, Graphics& g, int width, int height, bool selected) override
    {
        if (! isPositiveAndBelow (row, (int) rows.size())) return;
        if (selected)
        {
            g.setColour (HostDesign::selection);
            g.fillRoundedRectangle (1, 2, (float) width - 2, (float) height - 4, 6);
        }
        g.setColour (HostDesign::border);
        g.drawHorizontalLine (height - 1, 0, (float) width);
        HostDesign::drawSymbol (g, HostDesign::effect, juce::Rectangle<float> (8, 32, 22, 22), HostDesign::secondary);
        g.setColour (HostDesign::text);
        g.setFont (Font (17.0f, Font::bold));
        g.drawText (rows[row].plugin.name, 42, 9, width - 104, 24, Justification::centredLeft);
        g.setColour (HostDesign::secondary);
        g.setFont (HostDesign::resolveUiFont(Font(15.0f)));
        const int count = rows[row].instanceCount;
        g.drawText (rows[row].manufacturer + (count > 0 ? " / " + String(count) + " in chain" : String()),
            42, 33, width - 104, 20, Justification::centredLeft);
        g.setFont (HostDesign::resolveUiFont(Font(14.0f)));
        g.drawText (rows[row].details, 42, 56, width - 48, 20, Justification::centredLeft);
    }

    Component* refreshComponentForRow (int row, bool, Component* existing) override
    {
        if (! isPositiveAndBelow (row, (int) rows.size())) { delete existing; return nullptr; }
        auto* control = dynamic_cast<AddControl*> (existing);
        if (control == nullptr) { delete existing; control = new AddControl (*this); }
        control->update (rows[row].plugin);
        return control;
    }
    void returnKeyPressed (int row) override { if (isPositiveAndBelow (row, (int) rows.size())) add (rows[row].plugin); }
    void listBoxItemDoubleClicked (int row, const MouseEvent&) override { returnKeyPressed (row); }
    static String metadata (const PluginDescription& plugin)
    {
        StringArray parts;
        parts.add(plugin.pluginFormatName);
        if (plugin.category.isNotEmpty()) parts.add(plugin.category.replaceCharacter('|', '/'));
        if (plugin.version.isNotEmpty()) parts.add("v" + plugin.version.trimCharactersAtStart("vV"));
        return parts.joinIntoString(" / ");
    }

    const PluginDescription* selectedPlugin() const
    {
        const int row = list.getSelectedRow();
        return isPositiveAndBelow(row, (int) rows.size()) ? &rows[row].plugin : nullptr;
    }

    void removeFromLibrary (const PluginDescription& plugin)
    {
        const PluginDescription copy(plugin);
        for (int i = known.getNumTypes(); --i >= 0;)
            if (PluginState::sameIdentity(copy, *known.getType(i))) known.removeType(i);
        refresh();
    }

    void deleteKeyPressed (int row) override
    {
        if (isPositiveAndBelow(row, (int) rows.size())) removeFromLibrary(rows[row].plugin);
    }

    void scanFor (int index)
    {
        if (formats == nullptr || settings == nullptr || !isPositiveAndBelow(index, formats->getNumFormats())) return;
        // Keep JUCE's scanner and its progress/folder dialogs, without displaying its library table.
        if (scanner == nullptr)
        {
            scanner = new PluginListComponent(*formats, known,
                settings->getFile().getSiblingFile("RecentlyCrashedPluginsList"), settings);
            addChildComponent(scanner);
        }
        if (!scanner->isScanning()) scanner->scanFor(*formats->getFormat(index));
    }

    void showScanMenu()
    {
        if (formats == nullptr) return;
        PopupMenu menu;
        int count = 0, onlyFormat = -1;
        for (int i = 0; i < formats->getNumFormats(); ++i)
            if (formats->getFormat(i)->canScanForPlugins())
            {
                menu.addItem(i + 1, "Scan for " + formats->getFormat(i)->getName() + " plugins...");
                onlyFormat = i;
                ++count;
            }
        if (count == 1) scanFor(onlyFormat);
        else if (count > 1)
        {
            Component::SafePointer<PluginLibraryView> safe(this);
            menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&scanButton),
                ModalCallbackFunction::create([safe] (int id) mutable { if (safe != nullptr && id > 0) safe->scanFor(id - 1); }));
        }
    }

    void showLibraryOptions()
    {
        const auto* selected = selectedPlugin();
        const PluginDescription selection = selected != nullptr ? *selected : PluginDescription();
        const bool hasSelection = selected != nullptr;
        PopupMenu menu, sort, skipped;
        const char* sorts[] = { "Name", "Manufacturer", "Format", "Category" };
        for (int i = 0; i < 4; ++i) sort.addItem(10 + i, sorts[i], true, sortBy == i);
        menu.addSubMenu("Sort by", sort);
        menu.addSeparator();
        menu.addColouredItem(1, "Remove selected from library", HostDesign::danger, hasSelection);
        menu.addItem(2, "Show plugin in folder", hasSelection && File::isAbsolutePath(selection.fileOrIdentifier)
            && File(selection.fileOrIdentifier).exists());
        menu.addColouredItem(3, "Remove missing plugins", HostDesign::danger, formats != nullptr && known.getNumTypes() > 0);
        menu.addColouredItem(4, "Clear library", HostDesign::danger, known.getNumTypes() > 0);
        const StringArray blacklist(known.getBlacklistedFiles());
        for (int i = 0; i < blacklist.size(); ++i)
            skipped.addItem(100 + i, File::createFileWithoutCheckingPath(blacklist[i]).getFileName());
        if (!blacklist.isEmpty())
        {
            menu.addSeparator();
            menu.addSubMenu("Allow skipped plugins to scan again", skipped);
        }
        Component::SafePointer<PluginLibraryView> safe(this);
        menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&manageButton),
            ModalCallbackFunction::create([safe, selection, blacklist] (int id) mutable
        {
            if (safe == nullptr || id == 0) return;
            if (id == 1) safe->removeFromLibrary(selection);
            else if (id == 2) File(selection.fileOrIdentifier).revealToUser();
            else if (id == 3 && safe->formats != nullptr)
            {
                for (int i = safe->known.getNumTypes(); --i >= 0;)
                    if (!safe->formats->doesPluginStillExist(*safe->known.getType(i))) safe->known.removeType(i);
            }
            else if (id == 4) safe->known.clear();
            else if (id >= 10 && id < 14) safe->sortBy = id - 10;
            else if (isPositiveAndBelow(id - 100, blacklist.size())) safe->known.removeFromBlacklist(blacklist[id - 100]);
            safe->refresh();
        }));
    }

    void listBoxItemClicked (int row, const MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            list.selectRow(row);
            showLibraryOptions();
        }
    }
    void textEditorTextChanged (TextEditor&) override { refresh(); }
    void textEditorEscapeKeyPressed (TextEditor&) override { searchBox.clear(); }
    void changeListenerCallback (ChangeBroadcaster* changed) override
    {
        if (changed == &active) refreshInstanceCounts();
        else refresh();
    }
    void buttonClicked (Button* button) override { if (button == &scanButton) showScanMenu(); else showLibraryOptions(); }

    KnownPluginList& known;
    PluginInstances& active;
    AudioPluginFormatManager* formats = nullptr;
    PropertiesFile* settings = nullptr;
    ScopedPointer<PluginListComponent> scanner;
    int sortBy = 0;
    std::vector<Row> rows;
    SearchEditor searchBox;
    ListBox list;
    Label empty;
    TextButton scanButton { "Scan for plugins..." }, manageButton { "Library options..." };
};
#endif
