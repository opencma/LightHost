#pragma once

#include <JuceHeader.h>

inline juce::StringArray connectStereo (juce::AudioProcessorGraph& graph,
                                       juce::AudioProcessorGraph::NodeID from,
                                       juce::AudioProcessorGraph::NodeID to)
{
    juce::StringArray errors;
    const auto source = graph.getNodeForId (from);
    const auto destination = graph.getNodeForId (to);
    if (source == nullptr || destination == nullptr)
    {
        errors.add ("An audio graph node was not created.");
        return errors;
    }

    const int outputs = source->getProcessor()->getTotalNumOutputChannels();
    const int inputs = destination->getProcessor()->getTotalNumInputChannels();
    const int channels = juce::jmin (2, inputs);
    for (int channel = 0; channel < channels; ++channel)
    {
        const int sourceChannel = outputs == 1 ? 0 : channel;
        if (sourceChannel >= outputs
            || ! graph.addConnection ({ { from, sourceChannel }, { to, channel } }))
            errors.add ("Could not connect audio channel " + juce::String (channel + 1) + ".");
    }
    return errors;
}

inline juce::StringArray connectIndependentChannelInput (juce::AudioProcessorGraph& graph,
                                                        juce::AudioProcessorGraph::NodeID from,
                                                        juce::AudioProcessorGraph::NodeID to,
                                                        int sourceChannel)
{
    juce::StringArray errors;
    const auto source = graph.getNodeForId (from);
    const auto destination = graph.getNodeForId (to);
    if (source == nullptr || destination == nullptr)
    {
        errors.add ("An audio graph node was not created.");
        return errors;
    }
    const int sourceChannels = source->getProcessor()->getTotalNumOutputChannels();
    const int inputChannels = destination->getProcessor()->getBusesLayout().getMainInputChannels();
    if (! juce::isPositiveAndBelow (sourceChannel, sourceChannels) || inputChannels < 1)
    {
        errors.add ("The selected independent channel is not supported by this plugin.");
        return errors;
    }
    for (int channel = 0; channel < inputChannels; ++channel)
        if (! graph.addConnection ({ { from, sourceChannel }, { to, channel } }))
            errors.add ("Could not connect an input to the independent channel plugin.");
    return errors;
}

inline juce::StringArray connectIndependentChannelOutput (juce::AudioProcessorGraph& graph,
                                                         juce::AudioProcessorGraph::NodeID from,
                                                         juce::AudioProcessorGraph::NodeID to,
                                                         int sourceChannel,
                                                         int destinationChannel)
{
    juce::StringArray errors;
    const auto source = graph.getNodeForId (from);
    const auto destination = graph.getNodeForId (to);
    if (source == nullptr || destination == nullptr)
    {
        errors.add ("An audio graph node was not created.");
        return errors;
    }
    if (! juce::isPositiveAndBelow (sourceChannel, source->getProcessor()->getTotalNumOutputChannels())
        || ! juce::isPositiveAndBelow (destinationChannel, destination->getProcessor()->getTotalNumInputChannels())
        || ! graph.addConnection ({ { from, sourceChannel }, { to, destinationChannel } }))
        errors.add ("Could not connect an independent channel plugin to its output.");
    return errors;
}
