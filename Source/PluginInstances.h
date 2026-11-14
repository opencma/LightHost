#ifndef LIGHTHOST_PLUGIN_INSTANCES_H
#define LIGHTHOST_PLUGIN_INSTANCES_H

#include "PluginState.h"
#include <algorithm>

// A library description identifies a binary; a chain instance identifies one
// independent processor made from it. Chain copies must retain instance identity;
// library and backup binary metadata deliberately use PluginDescription instead.
struct PluginInstance : public PluginDescription
{
    PluginInstance() = default;
    explicit PluginInstance(const PluginDescription& description)
        : PluginDescription(description), instanceId(Uuid().toString()) {}

    String instanceId;
    bool needsLegacyMigration = false;

    String settingsKey(const String& type) const
    {
        return "plugin-instance-v1-" + instanceId + "-" + type;
    }

    String displayName(const PropertySet& settings) const
    {
        return settings.getValue(settingsKey("display-name"), name);
    }

    static bool validId(const String& id)
    {
        return id.length() == 32 && !Uuid(id).isNull() && Uuid(id).toString() == id;
    }
};

class PluginInstances : public ChangeBroadcaster
{
public:
    int getNumTypes() const { return (int) entries.size(); }
    const PluginInstance* getType(int index) const
    {
        return isPositiveAndBelow(index, getNumTypes()) ? &entries[index] : nullptr;
    }

    PluginInstance addType(const PluginDescription& plugin)
    {
        PluginInstance instance(plugin);
        entries.push_back(instance);
        sendChangeMessage();
        return instance;
    }

    PluginInstance addInstance(const PluginDescription& plugin, PropertySet& settings)
    {
        const auto previous = ordered(settings);
        int order = 0;
        bool another = false;
        StringArray names;
        for (const auto& entry : previous)
        {
            settings.setValue(entry.settingsKey("order"), ++order);
            names.add(entry.displayName(settings));
            another = another || (entry.isDuplicateOf(plugin) && entry.pluginFormatName == plugin.pluginFormatName);
        }
        const PluginInstance instance = addType(plugin);
        settings.setValue(instance.settingsKey("order"), order + 1);
        if (another)
        {
            int suffix = 2;
            while (names.contains(plugin.name + " " + String(suffix))) ++suffix;
            settings.setValue(instance.settingsKey("display-name"), plugin.name + " " + String(suffix));
        }
        return instance;
    }

    void removeType(int index)
    {
        if (isPositiveAndBelow(index, getNumTypes()))
        {
            entries.erase(entries.begin() + index);
            sendChangeMessage();
        }
    }

    bool removeInstance(const String& id, PropertySet& settings)
    {
        for (int i = 0; i < getNumTypes(); ++i)
            if (entries[i].instanceId == id)
            {
                for (const char* type : { "state", "bypass", "order", "display-name" })
                    settings.removeValue(entries[i].settingsKey(type));
                removeType(i);
                return true;
            }
        return false;
    }

    void move(int from, int to, PropertySet& settings)
    {
        auto list = ordered(settings);
        if (!isPositiveAndBelow(from, (int) list.size()) || !isPositiveAndBelow(to, (int) list.size()) || from == to)
            return;
        const PluginInstance entry = list[from];
        list.erase(list.begin() + from);
        list.insert(list.begin() + to, entry);
        for (int i = 0; i < (int) list.size(); ++i) settings.setValue(list[i].settingsKey("order"), i + 1);
        sendChangeMessage();
    }

    void clear() { entries.clear(); sendChangeMessage(); }

    std::vector<PluginInstance> ordered(const PropertySet& settings) const
    {
        struct OrderedInstance
        {
            const PluginInstance* instance;
            int order;
        };
        // Read settings once per instance, rather than allocating keys and
        // looking up properties again for every comparison during sorting.
        std::vector<OrderedInstance> sorted;
        sorted.reserve(entries.size());
        for (const auto& entry : entries)
            sorted.push_back({ &entry, settings.getIntValue(entry.settingsKey("order")) });
        std::stable_sort(sorted.begin(), sorted.end(), [](const OrderedInstance& a, const OrderedInstance& b)
        {
            return a.order < b.order;
        });
        std::vector<PluginInstance> result;
        result.reserve(entries.size());
        for (const auto& entry : sorted)
            result.push_back(*entry.instance);
        return result;
    }

    XmlElement* createXml() const
    {
        auto* xml = new XmlElement("KNOWNPLUGINS");
        for (const auto& entry : entries)
        {
            auto* child = entry.PluginDescription::createXml();
            child->setAttribute("instanceId", entry.instanceId);
            xml->addChildElement(child);
        }
        return xml;
    }

    void recreateFromXml(const XmlElement& xml)
    {
        entries.clear();
        StringArray ids;
        bool legacy = true;
        if (xml.hasTagName("KNOWNPLUGINS"))
            forEachXmlChildElementWithTagName(xml, child, "PLUGIN")
            {
                PluginInstance instance;
                if (!instance.loadFromXml(*child)) continue;
                instance.instanceId = child->getStringAttribute("instanceId");
                instance.needsLegacyMigration = !child->hasAttribute("instanceId");
                legacy = legacy && instance.needsLegacyMigration;
                if (!PluginInstance::validId(instance.instanceId) || ids.contains(instance.instanceId))
                    instance.instanceId = Uuid().toString();
                ids.add(instance.instanceId);
                entries.push_back(instance);
            }
        // Match KnownPluginList's insertion order when upgrading a pre-instance list.
        if (legacy) std::reverse(entries.begin(), entries.end());
        sendChangeMessage();
    }

    void migrateLegacySettings(PropertySet& settings)
    {
        for (auto& entry : entries)
        {
            if (!entry.needsLegacyMigration) continue;
            for (const char* type : { "state", "bypass", "order", "display-name" })
            {
                const String source = PluginState::key(type, entry);
                const String target = entry.settingsKey(type);
                if (!settings.containsKey(target) && settings.containsKey(source))
                    settings.setValue(target, settings.getValue(source));
            }
            entry.needsLegacyMigration = false;
        }
        // Persist IDs in the same settings transaction as the migrated state.
        ScopedPointer<XmlElement> xml(createXml());
        settings.setValue("pluginListActive", xml);
    }

private:
    std::vector<PluginInstance> entries;
};

#endif
