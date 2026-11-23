#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ============================================================================
//  Interface de Typhon. Tout le détail vit dans PluginEditor.cpp (classe Ui).
//  L'interface est dessinée en 1280 x 800 puis mise à l'échelle :
//  la fenêtre se redimensionne librement en gardant ses proportions.
// ============================================================================
class TyphonEditor : public juce::AudioProcessorEditor
{
public:
    explicit TyphonEditor (TyphonProcessor&);
    ~TyphonEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void applyScaleSetting();

    struct Ui;
private:
    TyphonProcessor& proc;
    std::unique_ptr<Ui> ui;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TyphonEditor)
};
