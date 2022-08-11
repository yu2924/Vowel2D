//
//  PluginProcessor.cpp
//  Vowel2D_SharedCode
//
//  created by yu2924 on 2022-08-11
//  (c) 2022 yu2924
//

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PulseInstrument.h"
#include "FABB/BLT.h"

// ================================================================================

struct SharedResource
{
	SharedResource()
	{
		if(juce::LookAndFeel_V4* lf4 = dynamic_cast<juce::LookAndFeel_V4*>(&juce::LookAndFeel::getDefaultLookAndFeel()))
		{
			lf4->setColourScheme(juce::LookAndFeel_V4::getLightColourScheme());
		}
	}
};

// ================================================================================
// parameters

static const std::array<char*,VPID::Count> ParamTableProfile =
{
	//			 nameunit		 param			 valueconvert					 stringconvert
	"F1F"	"\t" "F1F;Hz"	"\t" "0~1;0.4"	"\t" "exp!0~1!200~5000"			"\t" "exp!0~1!200~5000!%.3k,x!%k,x",
	"F1Q"	"\t" "F1Q;"		"\t" "0~1;1"	"\t" "exp!0~1!1~20"				"\t" "exp!0~1!1~20!%.1f,x!%f,x",
	"F1A"	"\t" "F1A;dB"	"\t" "0~1;0.5"	"\t" "exp!0~1!0.1~10"			"\t" "lin!0~1!-20~20!%.1f,x!%f,x",
	"F2F"	"\t" "F2F;Hz"	"\t" "0~1;0.5"	"\t" "exp!0~1!200~5000"			"\t" "exp!0~1!200~5000!%.3k,x!%k,x",
	"F2Q"	"\t" "F2Q;"		"\t" "0~1;1"	"\t" "exp!0~1!1~20"				"\t" "exp!0~1!1~20!%.1f,x!%f,x",
	"F2A"	"\t" "F2A;dB"	"\t" "0~1;0.5"	"\t" "exp!0~1!0.1~10"			"\t" "lin!0~1!-20~20!%.1f,x!%f,x",
	"Gain"	"\t" "Gain;dB"	"\t" "0~1;0.75"	"\t" "pt!0!0; exp!0~1!0.01~1"	"\t" "pt!0!Off; lin!0~1!-40~0!%.1f,x!%f,x",
};

class V2DBoundParameter : public juce::AudioProcessorParameterWithID
{
public:
	Vowel2DAudioProcessor* mProcessor;
	const FABB::ParamConverter* mParamConverter;
	VPID mVpid;
	V2DBoundParameter(Vowel2DAudioProcessor* p, VPID pid)
		: AudioProcessorParameterWithID(p->getParamConverter(pid)->Key(), p->getParamConverter(pid)->Name(), juce::AudioProcessorParameterWithIDAttributes().withLabel(p->getParamConverter(pid)->Unit()))
		, mProcessor(p)
		, mParamConverter(p->getParamConverter(pid))
		, mVpid(pid)
	{}
	virtual float getValue() const override { return mProcessor->getParamValue(mVpid); }
	virtual void setValue(float v) override { mProcessor->setParamValue(mVpid, v); }
	virtual float getDefaultValue() const override { return mParamConverter->ControlDef(); }
	virtual int getNumSteps() const override
	{
		if(mParamConverter->IsEnum()) return mParamConverter->GetEnumCount() - 1;
		if(mParamConverter->IsInteger()) return std::abs((int)mParamConverter->NativeMax() - (int)mParamConverter->NativeMin());
		return 0x7fffffff;
	}
	virtual bool isDiscrete() const override { return mParamConverter->IsEnum() || mParamConverter->IsInteger(); }
	virtual juce::String getText(float v, int) const override { return mParamConverter->Format(v); }
	virtual float getValueForText(const juce::String& s) const override { return mParamConverter->Parse(s.toStdString()); }
};

// ================================================================================
// fft and double buffer

class FFTBufferImpl : public FFTBuffer
{
public:
	juce::AudioBuffer<float> buffer;
	int reallength = 0;
	int writepos = 0;
	bool ready = false;
	FFTBufferImpl(int real_len) : reallength(real_len) {}
	virtual ~FFTBufferImpl() override {}
	virtual int getRealLength() const override { return reallength; }
	virtual int getNumChannels() const override { return buffer.getNumChannels(); }
	virtual float** getWritePtrArray() override { return buffer.getArrayOfWritePointers(); }
	virtual const float* const* getReadPtrArray() const override { return buffer.getArrayOfReadPointers(); }
	virtual bool isReady() const override { return ready; }
	virtual void setNumChannels(int cch) override
	{
		buffer.setSize(cch, reallength * 2);
		buffer.clear();
		writepos = 0;
		ready = false;
	}
	virtual bool write(const float* const* pp, int cch, int len) override
	{
		int lw = std::min(len, reallength - writepos);
		if(lw <= 0) return false;
		for(int ich = 0; ich < cch; ++ich)
		{
			juce::FloatVectorOperations::copy(buffer.getWritePointer(ich, writepos), pp[ich], lw);
		}
		writepos += lw;
		if(reallength <= writepos)
		{
			ready = true;
			return true;
		}
		return false;
	}
	virtual void clear() override
	{
		buffer.clear();
		writepos = 0;
		ready = false;
	}
};

FFTBuffer* FFTBuffer::createInstance(int real_len)
{
	return new FFTBufferImpl(real_len);
}

struct FFTHive
{
	juce::dsp::FFT fft;
	juce::dsp::WindowingFunction<float> wndfnc;
	enum { IWriteBuf = 0, IReadBuf = 1 };
	juce::OwnedArray<FFTBuffer> buffers; // double buffer
	FFTHive(int order)
		: fft(order)
		, wndfnc(fft.getSize(), juce::dsp::WindowingFunction<float>::WindowingMethod::blackmanHarris)
	{
		buffers.add(FFTBuffer::createInstance(fft.getSize()));
		buffers.add(FFTBuffer::createInstance(fft.getSize()));
	}
	void prepare(int cch)
	{
		for(auto fb : buffers)
		{
			juce::ScopedLock slf(*fb);
			fb->setNumChannels(cch);
		}
	}
	void unprepare()
	{
		for(auto fb : buffers)
		{
			juce::ScopedLock slf(*fb);
			fb->setNumChannels(0);
		}
	}
	const FFTBuffer& rdbuf() const
	{
		return *buffers[IReadBuf];
	}
	FFTBuffer& wrbuf()
	{
		return *buffers[IWriteBuf];
	}
	void transformAndFlip()
	{
		FFTBuffer& fb = *buffers[IWriteBuf];
		int cch = fb.getNumChannels();
		float** pp = fb.getWritePtrArray();
		int reallen = fb.getRealLength();
		for(int ich = 0; ich < cch; ++ich)
		{
			float* p = pp[ich];
			wndfnc.multiplyWithWindowingTable(p, reallen);
			fft.performFrequencyOnlyForwardTransform(p);
		}
		buffers.swap(IWriteBuf, IReadBuf);
	}
};

// ================================================================================
// the processor

class Vowel2DAudioProcessorImpl
	: public Vowel2DAudioProcessor
{
private:
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Vowel2DAudioProcessorImpl)
public:
	static constexpr double DefaultSampleRate = 44100;
	juce::SharedResourcePointer<SharedResource> mSharedResource;
	FABB::ParamConverterTable mParamConverterTable;
	std::array<float, VPID::Count> mChunk;
	PulseInstrument mInstrument;
	juce::AudioBuffer<float> mInstBuffer;
	juce::AudioBuffer<float> mProcessBuffer;
	std::vector<FABB::RBJAFilterF> mF1BPFs;
	std::vector<FABB::RBJAFilterF> mF2BPFs;
	float mGain = 1;
	struct
	{
		double samplerate;
		int buffrsize;
		int cchBusInput;
		int cchBusOutput;
		int cchProcess;
		bool prepared;
	} mRunStat = {};
	FFTHive mFFTHive;
	juce::ListenerList<Listener> mListeners;
	// --------------------------------------------------------------------------------
	Vowel2DAudioProcessorImpl()
		: Vowel2DAudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true))
		, mParamConverterTable(ParamTableProfile.data(), ParamTableProfile.size())
		, mFFTHive(12)
	{
		for(int pid = 0; pid < VPID::Count; ++pid)
		{
			const FABB::ParamConverter* pc = mParamConverterTable[pid];
			mChunk[pid] = pc->ControlDef();
			addParameter(new V2DBoundParameter(this, (VPID)pid));
		}
		mF1BPFs.resize(1); mF1BPFs[0].SetType(FABB::RBJAFilterF::Type::BP);
		mF2BPFs.resize(1); mF2BPFs[0].SetType(FABB::RBJAFilterF::Type::BP);
		for(auto pid : { VPID::F1F, VPID::F1A, VPID::F1Q, VPID::F2F, VPID::F2A, VPID::F2Q })
		{
			setParamValue(pid, mChunk[pid]);
		}
		mInstrument.SetLFORate(5);
		mInstrument.SetModRange(2);
		mInstrument.SetBendRange(5);
	}
	virtual ~Vowel2DAudioProcessorImpl() override
	{
	}
	// --------------------------------------------------------------------------------
	// attributes
	virtual const juce::String getName() const override { return JucePlugin_Name; }
	virtual bool acceptsMidi() const override { return true; }
	virtual bool producesMidi() const override { return false; }
	virtual bool isMidiEffect() const override { return false; }
	virtual double getTailLengthSeconds() const override { return 0; }
	// --------------------------------------------------------------------------------
	// programs
	virtual int getNumPrograms() override { return 1; }
	virtual int getCurrentProgram() override { return 0; }
	virtual void setCurrentProgram(int) override {}
	virtual const juce::String getProgramName(int) override { return {}; }
	virtual void changeProgramName(int, const juce::String&) override {}
	// --------------------------------------------------------------------------------
	// editor
	virtual juce::AudioProcessorEditor* createEditor() override { return Vowel2DAudioProcessorEditor::createInstance(*this); }
	virtual bool hasEditor() const override { return true; }
	// --------------------------------------------------------------------------------
	// persistence
	static constexpr uint32_t PersistenceVersion = 0x00000001;
	void getStateInformation(juce::MemoryBlock& mb) override
	{
		juce::MemoryOutputStream str(mb, true);
		str.writeInt(PersistenceVersion);
		str.write(mChunk.data(), mChunk.size() * sizeof(float));
	}
	void setStateInformation(const void* p, int cb) override
	{
		juce::MemoryInputStream str(p, cb, false);
		if(str.readInt() == PersistenceVersion)
		{
			str.read(mChunk.data(), (int)(mChunk.size() * sizeof(float)));
			for(int pid = 0; pid < VPID::Count; ++pid) setParamValue((VPID)pid, mChunk[pid]);
		}
	}
	// --------------------------------------------------------------------------------
	// process
	virtual bool isBusesLayoutSupported(const BusesLayout& layouts) const override
	{
		int cchi = layouts.getNumChannels(true, 0);
		int ccho = layouts.getNumChannels(false, 0);
		if((0 < cchi) && (cchi == ccho)) return true; // n:n
		if(((cchi == 0) || (cchi == 1)) && (0 < ccho)) return true; // 0:n or 1:n
		return false;
	}
	virtual void prepareToPlay(double fs, int lbuf) override
	{
		BusesLayout layouts = getBusesLayout();
		mRunStat.samplerate = fs;
		mRunStat.buffrsize = lbuf;
		mRunStat.cchBusInput = layouts.getNumChannels(true, 0);
		mRunStat.cchBusOutput = layouts.getNumChannels(false, 0);
		mRunStat.cchProcess = std::max(1, mRunStat.cchBusInput);
		mRunStat.prepared = true;
		mInstrument.Prepare(fs);
		mInstBuffer.setSize(1, lbuf);
		mProcessBuffer.setSize(mRunStat.cchProcess, lbuf);
		mF1BPFs.resize(mRunStat.cchProcess);
		mF2BPFs.resize(mRunStat.cchProcess);
		for(auto&& bpf : mF1BPFs) bpf.SetType(FABB::RBJAFilterF::Type::BPVPG);
		for(auto&& bpf : mF2BPFs) bpf.SetType(FABB::RBJAFilterF::Type::BPVPG);
		for(int pid = 0; pid < VPID::Count; ++pid) setParamValue((VPID)pid, mChunk[pid]);
		mFFTHive.prepare(mRunStat.cchProcess);
		mListeners.call(&Listener::v2dRunStatDidChange);
	}
	virtual void releaseResources() override
	{
		mInstrument.Unprepare();
		mInstBuffer.setSize(0, 0);
		mProcessBuffer.setSize(0, 0);
		mFFTHive.unprepare();
		mRunStat.prepared = false;
		mListeners.call(&Listener::v2dRunStatDidChange);
	}
	virtual void processBlock(juce::AudioBuffer<float>& asb, juce::MidiBuffer& mb) override
	{
		juce::ScopedNoDenormals noDenormals;
		int lbuf = asb.getNumSamples();;
		int cchi = mRunStat.cchBusInput;
		int ccho = mRunStat.cchBusOutput;
		int cchp = mRunStat.cchProcess;
		// input
		mProcessBuffer.clear();
		if(0 < cchi)
		{
			jassert(cchi == cchp);
			for(int ich = 0; ich < cchi; ++ich)
			{
				juce::FloatVectorOperations::copy(mProcessBuffer.getWritePointer(ich), asb.getReadPointer(ich), lbuf);
			}
		}
		// render the instrument
		mInstBuffer.clear();
		int ismp = 0;
		for(const juce::MidiMessageMetadata mm : mb)
		{
			mInstrument.Process(mInstBuffer.getWritePointer(0, ismp), mm.samplePosition - ismp);
			ismp = mm.samplePosition;
			switch(mm.data[0] & 0xf0U)
			{
				case 0x80U:
					mInstrument.NoteOff(mm.data[1]);
					break;
				case 0x90U:
					if(0 < mm.data[2]) mInstrument.NoteOn(mm.data[1]);
					else mInstrument.NoteOff(mm.data[1]);
					break;
				case 0xb0U:
					if(mm.data[1] == 1)
					{
						float vwh = (float)mm.data[2] / 127.0f;
						mInstrument.SetLFOModCtrl(vwh);
					}
					break;
				case 0xe0U:
				{
					int wh = (int)(((juce::uint16)mm.data[2] << 7) | (juce::uint16)mm.data[1]) - 8192;
					float vwh = (float)wh / 8192.0f;
					mInstrument.SetPitchBendCtrl(vwh);
					break;
				}
			}
		}
		if(ismp < lbuf) mInstrument.Process(mInstBuffer.getWritePointer(0, ismp), lbuf - ismp);
		for(int ich = 0; ich < cchp; ++ich)
		{
			juce::FloatVectorOperations::add(mProcessBuffer.getWritePointer(ich), mInstBuffer.getReadPointer(0), lbuf);
		}
		// process the filters
		for(int ich = 0; ich < cchp; ++ich)
		{
			mF1BPFs[ich].Process(mProcessBuffer.getWritePointer(ich), lbuf);
			mF2BPFs[ich].Process(mProcessBuffer.getWritePointer(ich), lbuf);
		}
		// fft
		bool xformed = false;
		{
			FFTBuffer& fftbuf = mFFTHive.wrbuf();
			juce::ScopedTryLock stl(fftbuf);
			if(stl.isLocked())
			{
				if(fftbuf.isReady()) fftbuf.clear();
				if(fftbuf.write(mProcessBuffer.getArrayOfReadPointers(), cchp, lbuf))
				{
					mFFTHive.transformAndFlip();
					xformed = true;
				}
			}
		}
		if(xformed) mListeners.call(&Listener::v2dFftBufferReady);
		// output
		if(cchp == 1) // 1:n
		{
			juce::FloatVectorOperations::copyWithMultiply(asb.getWritePointer(0), mProcessBuffer.getReadPointer(0), mGain, lbuf);
			for(int ich = 1; ich < ccho; ++ich)
			{
				juce::FloatVectorOperations::copy(asb.getWritePointer(ich), asb.getReadPointer(0), lbuf);
			}
		}
		else // n:n
		{
			jassert(cchp == ccho);
			for(int ich = 0; ich < ccho; ++ich)
			{
				juce::FloatVectorOperations::copyWithMultiply(asb.getWritePointer(ich), mProcessBuffer.getReadPointer(ich), mGain, lbuf);
			}
		}
	}
	// --------------------------------------------------------------------------------
	// Vowel2DAudioProcessor
	virtual const FABB::ParamConverter* getParamConverter(VPID pid) const override
	{
		return mParamConverterTable[pid];
	}
	virtual float getParamValue(VPID pid) const override
	{
		return mChunk[pid];
	}
	virtual void setParamValue(VPID pid, float vc) override
	{
		juce::ScopedLock sl(getCallbackLock());
		const FABB::ParamConverter* pc = mParamConverterTable[pid];
		mChunk[pid] = juce::jlimit(pc->ControlMin(), pc->ControlMax(), vc);
		switch(pid)
		{
			case VPID::F1F: { float f = (float)(pc->ControlToNative(vc) / mRunStat.samplerate); for(auto&& bpf : mF1BPFs) bpf.SetFreq(f); break; }
			case VPID::F1Q: { float q = pc->ControlToNative(vc); for(auto&& bpf : mF1BPFs) bpf.SetQ(q); break; }
			case VPID::F1A: { float a = pc->ControlToNative(vc); for(auto&& bpf : mF1BPFs) bpf.SetA(a); break; }
			case VPID::F2F: { float f = (float)(pc->ControlToNative(vc) / mRunStat.samplerate); for(auto&& bpf : mF2BPFs) bpf.SetFreq(f); break; }
			case VPID::F2Q: { float q = pc->ControlToNative(vc); for(auto&& bpf : mF2BPFs) bpf.SetQ(q); break; }
			case VPID::F2A: { float a = pc->ControlToNative(vc); for(auto&& bpf : mF2BPFs) bpf.SetA(a); break; }
			case VPID::Gain: mGain = pc->ControlToNative(vc); break;
			default: break;;
		}
	}
	virtual void addListener(Listener* p) override
	{
		mListeners.add(p);
	}
	virtual void removeListener(Listener* p) override
	{
		mListeners.remove(p);
	}
	virtual bool isRunning() const override
	{
		return mRunStat.prepared;
	}
	virtual double getSamplerate() const override
	{
		return mRunStat.samplerate;
	}
	virtual void getFilterCoef(int i, FABB::IIR2F::Coef* p) const override
	{
		juce::ScopedLock sl(getCallbackLock());
		switch(i)
		{
			case 0: mF1BPFs.front().GetCoefficients(p); break;
			case 1: mF2BPFs.front().GetCoefficients(p); break;
		}
	}
	virtual const FFTBuffer& getFFTBuffer() const override
	{
		juce::ScopedLock sl(getCallbackLock());
		return mFFTHive.rdbuf();
	}
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new Vowel2DAudioProcessorImpl();
}
