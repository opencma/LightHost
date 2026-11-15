#pragma once

#include <functional>
#include <memory>

namespace juce { class String; }

class GlobalBypassShortcut
{
public:
    explicit GlobalBypassShortcut (std::function<void()> callback);
    ~GlobalBypassShortcut();

    juce::String setShortcut (const juce::String& description);
    void poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
