#include <juce_core/juce_core.h>
#include <iostream>

int main()
{
    int status = 0;
    auto stream = juce::URL ("https://annaspannertechsolutions.uk/lighthost/latest").createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (15000)
            .withStatusCode (&status));
    if (stream == nullptr || status != 200)
    {
        std::cerr << "HTTPS manifest request failed with status " << status << '\n';
        return 1;
    }

    const auto manifest = juce::JSON::parse (stream->readEntireStreamAsString());
    if (! manifest.isObject() || ! manifest.getProperty ("assets", {}).isObject()
        || manifest.getProperty ("version", {}).toString().isEmpty())
    {
        std::cerr << "HTTPS manifest response was not a valid update manifest\n";
        return 1;
    }

    std::cout << "HTTPS manifest request succeeded\n";
    return 0;
}
