//
//  PluginEditor.h
//  Vowel2D_SharedCode
//
//  created by yu2924 on 2022-08-11
//  (c) 2022 yu2924
//

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class Vowel2DAudioProcessorEditor : public juce::AudioProcessorEditor
{
protected:
	Vowel2DAudioProcessorEditor(Vowel2DAudioProcessor& p) : juce::AudioProcessorEditor(p) {}
public:
	virtual ~Vowel2DAudioProcessorEditor() override {}
	static Vowel2DAudioProcessorEditor* createInstance(Vowel2DAudioProcessor&);
};
