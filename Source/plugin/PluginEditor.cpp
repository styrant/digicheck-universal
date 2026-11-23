#include "PluginEditor.h"

DcuEditor::DcuEditor (DcuProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.getCore().settings;
    const int w = s.editorW, h = s.editorH;          // read first: setting the limits resizes (and saves) the size
    build();
    setResizable (true, true);
    setResizeLimits (900, 520, 6000, 4000);
    setSize (w, h);
    startTimerHz (4);
}

DcuEditor::~DcuEditor()
{
    mc.reset();
    setLookAndFeel (nullptr);
}

void DcuEditor::build()
{
    mc.reset();
    mc = std::make_unique<MainComponent> (proc.globalProps(), proc.getCore(), proc.getEngine(), nullptr, &proc);
    addAndMakeVisible (*mc);
    mc->setBounds (getLocalBounds());
}

void DcuEditor::resized()
{
    if (mc != nullptr) mc->setBounds (getLocalBounds());
    auto& s = proc.getCore().settings;
    if (getWidth() >= 900 && getHeight() >= 520) { s.editorW = getWidth(); s.editorH = getHeight(); }
}

void DcuEditor::timerCallback()
{
    if (proc.stateRestored.exchange (false)) build();   // the host loaded another state: rebuild the views on it
}
