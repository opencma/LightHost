#ifndef LIGHTHOST_PLUGIN_STATE_H
#define LIGHTHOST_PLUGIN_STATE_H

#include <vector>

namespace PluginState
{
inline bool sameIdentity(const PluginDescription& a, const PluginDescription& b)
{
    return a.uid == b.uid && a.pluginFormatName == b.pluginFormatName
        && a.fileOrIdentifier == b.fileOrIdentifier;
}

inline String encode(const String& value)
{
    return MemoryBlock(value.toRawUTF8(), value.getNumBytesAsUTF8()).toBase64Encoding();
}

inline String key(const String& type, const PluginDescription& plugin)
{
    // Use full identity rather than a path hash. Name/version changes retain settings.
    return "plugin-v2-" + type.toLowerCase() + "-" + encode(plugin.pluginFormatName)
        + "-" + String::toHexString(plugin.uid) + "-" + encode(plugin.fileOrIdentifier);
}

inline String legacyKey(const String& type, const PluginDescription& plugin)
{
    if (type == "display-name")
        return "plugin-display-name-" + plugin.createIdentifierString();
    return "plugin-" + type + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
}

template <typename ActiveList>
std::vector<PluginDescription> collectPlugins(const KnownPluginList& known, const ActiveList& active)
{
    std::vector<PluginDescription> result;
    const auto append = [&](const PluginDescription& plugin)
    {
        for (auto& existing : result)
            if (existing.isDuplicateOf(plugin) && existing.pluginFormatName == plugin.pluginFormatName)
            {
                existing = plugin;
                return;
            }
        result.push_back(plugin);
    };
    for (int i = 0; i < known.getNumTypes(); ++i) append(*known.getType(i));
    for (int i = 0; i < active.getNumTypes(); ++i) append(*active.getType(i));
    return result;
}

inline void migrateLegacySettings(PropertySet& settings, const std::vector<PluginDescription>& plugins)
{
    // Copy all entries before removing legacy keys: historic collisions must not
    // prevent the other plugin from retaining its last shared setting.
    for (const auto& plugin : plugins)
        for (const char* type : { "state", "bypass", "order", "display-name" })
        {
            const String oldKey = legacyKey(type, plugin);
            const String newKey = key(type, plugin);
            if (!settings.containsKey(newKey) && settings.containsKey(oldKey))
                settings.setValue(newKey, settings.getValue(oldKey));
        }
    for (const auto& plugin : plugins)
        for (const char* type : { "state", "bypass", "order", "display-name" })
            settings.removeValue(legacyKey(type, plugin));
}

inline void remapSettings(PropertySet& settings, const PluginDescription& before,
                          const PluginDescription& after)
{
    for (const char* type : { "state", "bypass", "order", "display-name" })
        for (bool legacy : { false, true })
        {
            const String oldKey = legacy ? legacyKey(type, before) : key(type, before);
            const String newKey = legacy ? legacyKey(type, after) : key(type, after);
            if (oldKey != newKey && settings.containsKey(oldKey))
            {
                settings.setValue(newKey, settings.getValue(oldKey));
                settings.removeValue(oldKey);
            }
        }
}

inline void capture(AudioProcessorGraph& graph, AudioProcessor& processor, MemoryBlock& state)
{
    // Match the audio callback's graph -> processor lock order, including plugins
    // that synchronously notify the host while their state is being captured.
    // Keep encoding and settings-file I/O outside this scope.
    const ScopedLock graphGuard(graph.getCallbackLock());
    const ScopedLock callbackGuard(processor.getCallbackLock());
    processor.getStateInformation(state);
}

inline void relocatePortableSettings(PropertySet& settings, const File& directory)
{
    const String previous = settings.getValue("portableAppDirectory");
    if (previous.isNotEmpty() && File::isAbsolutePath(previous) && File(previous) != directory)
    {
        const File oldDirectory(previous);
        for (const char* listKey : { "pluginList", "pluginListActive" })
        {
            ScopedPointer<XmlElement> list(XmlDocument::parse(settings.getValue(listKey)));
            if (list == nullptr)
                continue;
            forEachXmlChildElementWithTagName(*list, entry, "PLUGIN")
            {
                PluginDescription before;
                if (!before.loadFromXml(*entry) || !File::isAbsolutePath(before.fileOrIdentifier))
                    continue;
                const File oldPlugin(before.fileOrIdentifier);
                if (!oldPlugin.isAChildOf(oldDirectory))
                    continue;
                PluginDescription after(before);
                after.fileOrIdentifier = directory.getChildFile(oldPlugin.getRelativePathFrom(oldDirectory)).getFullPathName();
                remapSettings(settings, before, after);
                entry->setAttribute("file", after.fileOrIdentifier);
            }
            settings.setValue(listKey, list->createDocument(String(), true));
        }
        const StringArray keys(settings.getAllProperties().getAllKeys());
        for (const auto& property : keys)
            if (property.startsWith("lastPluginScanPath_"))
            {
                FileSearchPath paths(settings.getValue(property));
                FileSearchPath relocated;
                for (int i = 0; i < paths.getNumPaths(); ++i)
                    relocated.add(paths[i] == oldDirectory ? directory
                        : paths[i].isAChildOf(oldDirectory)
                            ? directory.getChildFile(paths[i].getRelativePathFrom(oldDirectory)) : paths[i]);
                settings.setValue(property, relocated.toString());
            }
    }
    settings.setValue("portableAppDirectory", directory.getFullPathName());
}
}

#endif
