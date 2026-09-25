#include "TestUtils.h"

#include <juce_events/juce_events.h>

void runCompressorTests();
void runProcessorTests();

int main()
{
    // The editor's components and parameter attachments need a message manager.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    runCompressorTests();
    runProcessorTests();

    std::printf ("\n%s\n", test::failures == 0 ? "ALL PASS" : "FAILURES ABOVE");
    return test::failures == 0 ? 0 : 1;
}
