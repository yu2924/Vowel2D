//
//  PluginProcessor.h
//  Vowel2D_SharedCode
//
//  created by yu2924 on 2022-08-11
//  (c) 2022 yu2924
//

#pragma once

#include <JuceHeader.h>
#include "FABB/ParamConvert.h"
#include "FABB//IIR.h"

class FFTBuffer : public juce::CriticalSection
{
public:
	FFTBuffer() {}
	virtual ~FFTBuffer() {}
	virtual int getRealLength() const = 0;
	virtual int getNumChannels() const = 0;
	virtual float** getWritePtrArray() = 0;
	virtual const float* const* getReadPtrArray() const = 0;
	virtual bool isReady() const = 0;
	virtual void setNumChannels(int cch) = 0;
	virtual bool write(const float* const* pp, int cch, int len) = 0;
	virtual void clear() = 0;
	// fft_size = real_len * 2
	static FFTBuffer* createInstance(int real_len);
};

enum VPID
{
	F1F,
	F1Q,
	F1A,
	F2F,
	F2Q,
	F2A,
	Gain,
	Count,
};

class Vowel2DAudioProcessor : public juce::AudioProcessor
{
public:
	struct Listener
	{
		virtual ~Listener() {}
		virtual void v2dRunStatDidChange() = 0;
		virtual void v2dFftBufferReady() = 0;
	};
	Vowel2DAudioProcessor(const BusesProperties& layouts) : juce::AudioProcessor(layouts) {}
	virtual const FABB::ParamConverter* getParamConverter(VPID pid) const = 0;
	virtual float getParamValue(VPID pid) const = 0;
	virtual void setParamValue(VPID pid, float vc) = 0;
	virtual void addListener(Listener*) = 0;
	virtual void removeListener(Listener*) = 0;
	virtual bool isRunning() const = 0;
	virtual double getSamplerate() const = 0;
	virtual void getFilterCoef(int i, FABB::IIR2F::Coef* p) const = 0;
	virtual const FFTBuffer& getFFTBuffer() const = 0;
};
