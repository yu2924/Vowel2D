//
//  PluginEditor.cpp
//  Vowel2D_SharedCode
//
//  created by yu2924 on 2022-08-11
//  (c) 2022 yu2924
//

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FABB/CurveMapping.h"

static const juce::Colour BackColor			{ 0xfff8f8f8 };
static const juce::Colour GridColor			{ 0xffc0c0c0 };
static const juce::Colour LabelColor		{ 0xff202020 };
static const juce::Colour MarkerColor		{ 0xffff0000 };
static const juce::Colour FilterPlotColor	{ 0xff0000ff };
static const juce::Colour FftPlotColor		{ 0xff008000 };

// ================================================================================

static juce::String getFrequencyFormat(float f)
{
	float fabs = std::abs(f);
	if     (fabs < 1e3f) return juce::String::formatted("%g", f);
	else if(fabs < 1e6f) return juce::String::formatted("%gk", f * 0.001f);
	else if(fabs < 1e9f) return juce::String::formatted("%gM", f * 0.000001f);
	else				 return juce::String::formatted("%gG", f * 0.000000001f);
}

class AsyncInvoker : private juce::AsyncUpdater
{
public:
	std::function<void()> onUpdate;
	void trigger() { triggerAsyncUpdate(); }
	virtual void handleAsyncUpdate() override { if(onUpdate) onUpdate(); }
};

class RoundMarker : public juce::Component
{
public:
	std::function<void()> onDragBegin;
	std::function<void(juce::Point<int>)> onDragMove;
	std::function<void()> onDragEnd;
	std::function<void()> onDoubleClick;
	juce::Colour mFillColour;
	juce::Point<int> mDragOffset = {};
	virtual juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::StandardCursorType::DraggingHandCursor; }
	virtual void paint(juce::Graphics& g) override { g.setColour(mFillColour); g.fillEllipse(getLocalBounds().toFloat()); }
	virtual void mouseDown(const juce::MouseEvent& me) override { mDragOffset = me.getPosition() - getCenterPoint(); if(onDragBegin) onDragBegin(); }
	virtual void mouseDrag(const juce::MouseEvent& me) override { if(onDragMove) onDragMove(me.getPosition() - mDragOffset); }
	virtual void mouseUp(const juce::MouseEvent&) override { if(onDragEnd) onDragEnd(); }
	virtual void mouseDoubleClick(const juce::MouseEvent&) override { if(onDoubleClick) onDoubleClick(); }
	juce::Point<int> getCenterPoint() const { return { getWidth() / 2, getHeight() / 2 }; }
	void setCenterPoint(int x, int y) { setTopLeftPosition(x - getWidth() / 2, y - getHeight() / 2); }
	juce::Colour getFillColour() const { return mFillColour; }
	void setFillColour(const juce::Colour& v) { mFillColour = v; repaint(); }
};

// ================================================================================
// plot views

class VPlotView
	: public juce::Component
	, public juce::AudioProcessorParameter::Listener
{
public:
	Vowel2DAudioProcessor* mProcessor;
	juce::Font mFont = juce::Font(13, juce::Font::FontStyleFlags::plain);
	std::array<float, 5> mFreqGrid = { 200, 500, 1000, 2000, 5000 };
	int mLeftMargin = 40;
	juce::Rectangle<int> mPlotRect;
	FABB::CurveMapExponentialF mMapX2F;
	FABB::CurveMapExponentialF mMapY2F;
	struct Mkr
	{
		RoundMarker mk;
		juce::AudioProcessorParameter* paramf1 = nullptr;
		juce::AudioProcessorParameter* paramf2 = nullptr;
		const FABB::ParamConverter* pcf1 = nullptr;
		const FABB::ParamConverter* pcf2 = nullptr;
		float f1 = 0;
		float f2 = 0;
	} mMkr;
	AsyncInvoker mAsyncUpdateParams;
	enum { MarkerSize = 16, TypicalAreaSize = 40 };
	VPlotView(Vowel2DAudioProcessor* p)
		: mProcessor(p)
	{
		setOpaque(true);
		const auto& params = mProcessor->getParameters();
		mMkr.paramf1 = params[VPID::F1F];
		mMkr.paramf2 = params[VPID::F2F];
		mMkr.pcf1 = mProcessor->getParamConverter(VPID::F1F);
		mMkr.pcf2 = mProcessor->getParamConverter(VPID::F2F);
		mMkr.mk.setSize(MarkerSize, MarkerSize);
		mMkr.mk.setFillColour(MarkerColor);
		mMkr.mk.setInterceptsMouseClicks(false, false);
		addAndMakeVisible(mMkr.mk);
		mMkr.paramf1->addListener(this);
		mMkr.paramf2->addListener(this);
		mMkr.f1 = mMkr.pcf1->ControlToNative(mMkr.paramf1->getValue());
		mMkr.f2 = mMkr.pcf2->ControlToNative(mMkr.paramf2->getValue());
		mAsyncUpdateParams.onUpdate = [this]()
		{
			updateMarkerPosition();
		};
	}
	virtual ~VPlotView() override
	{
		mMkr.paramf1->removeListener(this);
		mMkr.paramf2->removeListener(this);
	}
	void updateMapping()
	{
		int cxlabel = juce::roundToInt(mFont.getStringWidthFloat(getFrequencyFormat(mFreqGrid.back())) + 1);
		int cylabel = juce::roundToInt(mFont.getHeight() + 1);
		juce::BorderSize<int> inset;
		inset.setLeft(mLeftMargin);
		inset.setRight(cxlabel / 2 + 2);
		inset.setTop(cylabel);
		inset.setBottom((cylabel * 3) / 2);
		mPlotRect = inset.subtractedFrom(getLocalBounds());
		mMapX2F.Setup((float)mPlotRect.getX(), (float)mPlotRect.getRight(), mFreqGrid.front(), mFreqGrid.back());
		mMapY2F.Setup((float)mPlotRect.getBottom(), (float)mPlotRect.getY(), mFreqGrid.front(), mFreqGrid.back());
	}
	void updateMarkerPosition()
	{
		mMkr.mk.setCenterPoint(f2x(mMkr.f1), f2y(mMkr.f2));
	}
	int f2x(float f) const { return juce::roundToInt(mMapX2F.Unmap(f)); }
	float x2f(int x) const { return mMapX2F.Map((float)x); }
	int f2y(float f) const { return juce::roundToInt(mMapY2F.Unmap(f)); }
	float y2f(int y) const { return mMapY2F.Map((float)y); }
	virtual void resized() override
	{
		updateMapping();
		updateMarkerPosition();
	}
	virtual void paint(juce::Graphics& g) override
	{
		g.fillAll(BackColor);
		g.setFont(mFont);
		float fontheight = mFont.getHeight();
		float fontascent = mFont.getAscent();
		// horz grid
		int xl = mPlotRect.getX();
		int xh = mPlotRect.getRight();
		for(float f : mFreqGrid)
		{
			int y = f2y(f);
			g.setColour(GridColor);
			g.drawHorizontalLine(y, (float)xl, (float)xh);
			g.setColour(LabelColor);
			g.drawSingleLineText(getFrequencyFormat(f), xl - juce::roundToInt(fontheight * 0.5f), y - juce::roundToInt(fontheight * 0.5f - fontascent), juce::Justification::right);
		}
		// vert grid
		int yl = mPlotRect.getBottom();
		int yh = mPlotRect.getY();
		for(float f : mFreqGrid)
		{
			int x = f2x(f);
			g.setColour(GridColor);
			g.drawVerticalLine(x, (float)yh, (float)yl);
			g.setColour(LabelColor);
			g.drawSingleLineText(getFrequencyFormat(f), x, yl + juce::roundToInt(fontheight * 0.5f + fontascent), juce::Justification::horizontallyCentred);
		}
		// typical areas
		static const struct { juce::String s; char t; float f1, f2; } TypicalAreas[] =
		{
			{"a", 'f', 888, 1363},
			{"e", 'f', 483, 2317},
			{"i", 'f', 325, 2725},
			{"o", 'f', 483,  925},
			{"u", 'f', 375, 1675},
			{"a", 'm', 775, 1163},
			{"e", 'm', 475, 1738},
			{"i", 'm', 263, 2263},
			{"o", 'm', 550,  838},
			{"u", 'm', 363, 1300},
		};
		for(const auto& ve : TypicalAreas)
		{
			int x = f2x(ve.f1);
			int y = f2y(ve.f2);
			juce::Colour clr;
			switch(ve.t)
			{
				case 'f': clr = juce::Colour(0x40ff8080); break;
				case 'm': clr = juce::Colour(0x408080ff); break;
			}
			g.setColour(clr);
			g.fillEllipse((float)(x - TypicalAreaSize / 2), (float)(y - TypicalAreaSize / 2), (float)TypicalAreaSize, (float)TypicalAreaSize);
			g.setColour(juce::Colour(0xc0000000));
			g.drawSingleLineText(ve.s, x, y - juce::roundToInt(fontheight * 0.5f - fontascent), juce::Justification::horizontallyCentred);
		}
	}
	virtual void mouseDown(const juce::MouseEvent& me) override
	{
		mMkr.paramf1->beginChangeGesture();
		mMkr.paramf2->beginChangeGesture();
		mMkr.paramf1->setValueNotifyingHost(mMkr.pcf1->NativeToControl(x2f(me.x)));
		mMkr.paramf2->setValueNotifyingHost(mMkr.pcf2->NativeToControl(y2f(me.y)));
	}
	virtual void mouseDrag(const juce::MouseEvent& me) override
	{
		mMkr.paramf1->setValueNotifyingHost(mMkr.pcf1->NativeToControl(x2f(me.x)));
		mMkr.paramf2->setValueNotifyingHost(mMkr.pcf2->NativeToControl(y2f(me.y)));
	}
	virtual void mouseUp(const juce::MouseEvent&) override
	{
		mMkr.paramf1->endChangeGesture();
		mMkr.paramf2->endChangeGesture();
	}
	virtual void mouseDoubleClick(const juce::MouseEvent& me) override
	{
		if(mMkr.mk.getBounds().contains(me.getPosition()))
		{
			mMkr.paramf1->setValueNotifyingHost(mMkr.paramf1->getDefaultValue());
			mMkr.paramf2->setValueNotifyingHost(mMkr.paramf2->getDefaultValue());
		}
	}
	// juce::AudioProcessorParameter::Listener
	virtual void parameterValueChanged(int pid, float) override
	{
		switch(pid)
		{
			case VPID::F1F: mMkr.f1 = mMkr.pcf1->ControlToNative(mMkr.paramf1->getValue()); break;
			case VPID::F2F: mMkr.f2 = mMkr.pcf2->ControlToNative(mMkr.paramf2->getValue()); break;
		}
		mAsyncUpdateParams.trigger();
	}
	virtual void parameterGestureChanged(int, bool) override
	{
	}
};

class FPlotView
	: public juce::Component
	, public juce::AudioProcessorParameter::Listener
	, public Vowel2DAudioProcessor::Listener
{
public:
	Vowel2DAudioProcessor* mProcessor;
	juce::Font mFont = juce::Font(13, juce::Font::FontStyleFlags::plain);
	std::array<float, 5> mFreqGrid = { 200, 500, 1000, 2000, 5000 };
	std::array<float, 5> mAmpGrid = { 0.01f, 0.1f, 1.0f, 10.0f, 100.0f };
	int mLeftMargin = 40;
	juce::Rectangle<int> mPlotRect;
	FABB::CurveMapExponentialF mMapX2F;
	FABB::CurveMapExponentialF mMapY2A;
	using cpx = std::complex<float>;
	std::vector<cpx> mXZTable;
	std::vector<float> mFiltPtTable;
	std::vector<std::vector<juce::Point<float> > > mFFTPlotTable;
	struct Mkr
	{
		RoundMarker mk;
		juce::AudioProcessorParameter* paramf = nullptr;
		juce::AudioProcessorParameter* paramq = nullptr;
		juce::AudioProcessorParameter* parama = nullptr;
		const FABB::ParamConverter* pcf = nullptr;
		const FABB::ParamConverter* pca = nullptr;
		float f = 0;
		float a = 0;
	};
	std::array<Mkr, 2> mMkrs;
	AsyncInvoker mAsyncUpdateParams;
	AsyncInvoker mAsyncUpdateRunStat;
	AsyncInvoker mAsyncUpdateFftBuf;
	enum { MarkerSize = 16 };
	FPlotView(Vowel2DAudioProcessor* p)
		: mProcessor(p)
	{
		setOpaque(true);
		const auto& params = mProcessor->getParameters();
		struct { VPID pidf, pidq, pida; } VPIDs[] =
		{
			{ VPID::F1F, VPID::F1Q, VPID::F1A },
			{ VPID::F2F, VPID::F2Q, VPID::F2A },
		};
		for(size_t c = mMkrs.size(), i = 0; i < c; ++i)
		{
			Mkr& mkr = mMkrs[i];
			auto vpids = VPIDs[i];
			mkr.paramf = params[vpids.pidf];
			mkr.paramq = params[vpids.pidq];
			mkr.parama = params[vpids.pida];
			mkr.pcf = mProcessor->getParamConverter(vpids.pidf);
			mkr.pca = mProcessor->getParamConverter(vpids.pida);
			mkr.mk.setSize(MarkerSize, MarkerSize);
			mkr.mk.setFillColour(MarkerColor);
			addAndMakeVisible(mkr.mk);
			mkr.paramf->addListener(this);
			mkr.paramq->addListener(this);
			mkr.parama->addListener(this);
			mkr.f = mkr.pcf->ControlToNative(mkr.paramf->getValue());
			mkr.a = mkr.pca->ControlToNative(mkr.parama->getValue());
			mkr.mk.onDragBegin = [&mkr]()
			{
				mkr.paramf->beginChangeGesture();
				mkr.parama->beginChangeGesture();
			};
			mkr.mk.onDragMove = [&mkr, this](juce::Point<int> pt)
			{
				juce::Point<int> ptl = getLocalPoint(&mkr.mk, pt);
				mkr.paramf->setValueNotifyingHost(mkr.pcf->NativeToControl(x2f(ptl.x)));
				mkr.parama->setValueNotifyingHost(mkr.pca->NativeToControl(y2a(ptl.y)));
			};
			mkr.mk.onDragEnd = [&mkr]()
			{
				mkr.paramf->endChangeGesture();
				mkr.parama->endChangeGesture();
			};
			mkr.mk.onDoubleClick = [&mkr]()
			{
				mkr.paramf->setValueNotifyingHost(mkr.paramf->getDefaultValue());
				mkr.parama->setValueNotifyingHost(mkr.parama->getDefaultValue());
			};
		}
		mAsyncUpdateParams.onUpdate = [this]()
		{
			updateMarkerPosition(0);
			updateMarkerPosition(1);
			updateFiltPtTable();
			repaint();
		};
		mAsyncUpdateRunStat.onUpdate = [this]()
		{
			updateXZTable();
			updateFiltPtTable();
			updateFftPlotTable();
			repaint();
		};
		mAsyncUpdateFftBuf.onUpdate = [this]()
		{
			updateFftPlotTable();
			repaint();
		};
		mProcessor->addListener(this);
	}
	virtual ~FPlotView() override
	{
		mProcessor->removeListener(this);
		for(size_t c = mMkrs.size(), i = 0; i < c; ++i)
		{
			Mkr& mkr = mMkrs[i];
			mkr.paramf->removeListener(this);
			mkr.paramq->removeListener(this);
			mkr.parama->removeListener(this);
		}
	}
	void updateMapping()
	{
		int cxlabel = juce::roundToInt(mFont.getStringWidthFloat(getFrequencyFormat(mFreqGrid.back())) + 1);
		int cylabel = juce::roundToInt(mFont.getHeight() + 1);
		juce::BorderSize<int> inset;
		inset.setLeft(mLeftMargin);
		inset.setRight(cxlabel / 2 + 2);
		inset.setTop(cylabel);
		inset.setBottom((cylabel * 3) / 2);
		mPlotRect = inset.subtractedFrom(getLocalBounds());
		mMapX2F.Setup((float)mPlotRect.getX(), (float)mPlotRect.getRight(), mFreqGrid.front(), mFreqGrid.back());
		mMapY2A.Setup((float)mPlotRect.getBottom(), (float)mPlotRect.getY(), mAmpGrid.front(), mAmpGrid.back());
	}
	void updateMarkerPosition(int i)
	{
		auto& mkr = mMkrs[i];
		mkr.mk.setCenterPoint(f2x(mkr.f), a2y(mkr.a));
	}
	void updateXZTable()
	{
		mXZTable.resize(mPlotRect.getWidth());
		int cx = mPlotRect.getWidth();
		float t = (float)(1 / mProcessor->getSamplerate());
		for(int x = 0; x < cx; ++x)
		{
			mXZTable[x] = std::exp(cpx(0.0f, -2.0f * juce::float_Pi * x2f(x + mLeftMargin) * t));
		}
	}
	void updateFiltPtTable()
	{
		mFiltPtTable.resize(mPlotRect.getWidth());
		FABB::IIR2F::Coef coef2[2];
		mProcessor->getFilterCoef(0, &coef2[0]);
		mProcessor->getFilterCoef(1, &coef2[1]);
		int cx = mPlotRect.getWidth();
		for(int x = 0; x < cx; ++x)
		{
			//        b0 + b1*z^-1 + b2*z^-2   b0*Z^2 + b1*z + b2
			// H(z) = ---------------------- = ------------------
			//         1 + a1*z^-1 + a2*z^-2      Z^2 + a1*z + a2
			cpx z = mXZTable[x];
			cpx z2 = z * z;
			float a = 1;
			for(const auto& coef : coef2)
			{
				cpx n = coef.b0 * z2 + coef.b1 * z + coef.b2;
				cpx d = z2 + coef.a1 * z + coef.a2;
				a *= std::abs(n / d);
			}
			mFiltPtTable[x] = (float)a2y(a);
		}
	}
	void updateFftPlotTable()
	{
		const FFTBuffer& fftbuf = mProcessor->getFFTBuffer();
		juce::ScopedLock sl(fftbuf);
		int cch = fftbuf.getNumChannels();
		int len = fftbuf.getRealLength();
		mFFTPlotTable.resize(cch);
		for(auto&& ft : mFFTPlotTable) { ft.reserve(len); ft.resize(0); }
		if(fftbuf.isReady())
		{
			float fmin = mFreqGrid.front(), fmax = mFreqGrid.back();
			float amin = mAmpGrid.front();
			float fs = (float)mProcessor->getSampleRate();
			float invlen = 1.0f / (float)len;
			const float* const* ppfftbuf = fftbuf.getReadPtrArray();
			for(int ich = 0; ich < cch; ++ich)
			{
				const float* pfftbuf = ppfftbuf[ich];
				std::vector<juce::Point<float> >& vpt = mFFTPlotTable[ich];
				for(int i = 0; i < len; ++i)
				{
					float f = fs * (float)i * invlen;
					if((f <= fmin) || (fmax <= f)) continue;
					float x = mMapX2F.Unmap(f);
					float a = std::max(amin, pfftbuf[i]) * 0.1f;
					float y = mMapY2A.Unmap(a);
					vpt.push_back({ x, y });
				}
			}
		}
	}
	int f2x(float f) const { return juce::roundToInt(mMapX2F.Unmap(f)); }
	float x2f(int x) const { return mMapX2F.Map((float)x); }
	int a2y(float a) const { return juce::roundToInt(mMapY2A.Unmap(a)); }
	float y2a(int y) const { return mMapY2A.Map((float)y); }
	virtual void resized() override
	{
		updateMapping();
		updateMarkerPosition(0);
		updateMarkerPosition(1);
		updateXZTable();
		updateFiltPtTable();
		updateFftPlotTable();
	}
	virtual void paint(juce::Graphics& g) override
	{
		g.fillAll(BackColor);
		g.setFont(mFont);
		float fontheight = mFont.getHeight();
		float fontascent = mFont.getAscent();
		// horz grid
		int xl = mPlotRect.getX();
		int xh = mPlotRect.getRight();
		for(float a : mAmpGrid)
		{
			int y = a2y(a);
			g.setColour(GridColor);
			g.drawHorizontalLine(y, (float)xl, (float)xh);
			g.setColour(LabelColor);
			g.drawSingleLineText(juce::String::formatted("%.1f", 20 * log10(a)), xl - juce::roundToInt(fontheight * 0.5f), y - juce::roundToInt(fontheight * 0.5f - fontascent), juce::Justification::right);
		}
		// vert grid
		int yl = mPlotRect.getBottom();
		int yh = mPlotRect.getY();
		for(float f : mFreqGrid)
		{
			int x = f2x(f);
			g.setColour(GridColor);
			g.drawVerticalLine(x, (float)yh, (float)yl);
			g.setColour(LabelColor);
			g.drawSingleLineText(getFrequencyFormat(f), x, yl + juce::roundToInt(fontheight * 0.5f + fontascent), juce::Justification::horizontallyCentred);
		}
		// fft
		{
			g.setColour(FftPlotColor);
			for(const auto& vpt : mFFTPlotTable)
			{
				if(!vpt.empty())
				{
					juce::Path path;
					auto it = vpt.begin();
					path.startNewSubPath(*it++);
					while(it != vpt.end())
					{
						path.lineTo(*it++);
					}
					g.strokePath(path, juce::PathStrokeType(1));
				}
			}
		}
		// filter
		{
			g.setColour(FilterPlotColor);
			if(!mFiltPtTable.empty())
			{
				juce::Path path;
				int x = mPlotRect.getX();
				auto it = mFiltPtTable.begin();
				path.startNewSubPath((float)(x++), *it++);
				while(it != mFiltPtTable.end())
				{
					path.lineTo((float)(x++), *it++);
				}
				g.strokePath(path, juce::PathStrokeType(1));
			}
		}
	}
	// juce::AudioProcessorParameter::Listener
	virtual void parameterValueChanged(int pid, float) override
	{
		switch(pid)
		{
			case VPID::F1F: mMkrs[0].f = mMkrs[0].pcf->ControlToNative(mMkrs[0].paramf->getValue()); break;
			case VPID::F1Q: break;
			case VPID::F1A: mMkrs[0].a = mMkrs[0].pca->ControlToNative(mMkrs[0].parama->getValue()); break;
			case VPID::F2F: mMkrs[1].f = mMkrs[1].pcf->ControlToNative(mMkrs[1].paramf->getValue()); break;
			case VPID::F2Q: break;
			case VPID::F2A: mMkrs[1].a = mMkrs[1].pca->ControlToNative(mMkrs[1].parama->getValue()); break;
		}
		mAsyncUpdateParams.trigger();
	}
	virtual void parameterGestureChanged(int, bool) override
	{
	}
	// Vowel2DAudioProcessor::Listener
	virtual void v2dRunStatDidChange() override
	{
		mAsyncUpdateRunStat.trigger();
	}
	virtual void v2dFftBufferReady() override
	{
		mAsyncUpdateFftBuf.trigger();
	}
};

// ================================================================================
// custon knob

class ParamBoundSlider
	: public juce::Component
	, public juce::AsyncUpdater
	, public juce::AudioProcessorParameter::Listener
{
public:
	juce::AudioProcessorParameter* mParam;
	juce::Label mLabel;
	juce::Slider mSlider;
	enum { LabelHeight = 15 };
	ParamBoundSlider(juce::AudioProcessorParameter* p)
		: mParam(p)
	{
		mParam->addListener(this);
		mLabel.setMinimumHorizontalScale(0.5f);
		mLabel.setJustificationType(juce::Justification::centred);
		mLabel.setText(mParam->getName(16), juce::NotificationType::dontSendNotification);
		addAndMakeVisible(mLabel);
		mSlider.setSliderStyle(juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag);
		mSlider.setColour(juce::Slider::ColourIds::textBoxBackgroundColourId, BackColor);
		mSlider.valueFromTextFunction = [this](const juce::String& s)->double { return mParam ? mParam->getValueForText(s) : 0; };
		mSlider.textFromValueFunction = [this](double v)->juce::String { return mParam ? mParam->getText((float)v, 256) : juce::String("---"); };
		mSlider.onDragStart = [this]() { if(mParam) mParam->beginChangeGesture(); };
		mSlider.onDragEnd = [this]() { if(mParam) mParam->endChangeGesture(); };
		mSlider.onValueChange = [this]() { if(mParam) mParam->setValueNotifyingHost((float)mSlider.getValue()); };
		double dv = mParam->isDiscrete() ? (1.0 / (double)mParam->getNumSteps()) : 0;
		mSlider.setRange(0, 1, dv);
		mSlider.setDoubleClickReturnValue(true, mParam->getDefaultValue());
		mSlider.setValue(mParam->getValue(), juce::NotificationType::dontSendNotification);
		addAndMakeVisible(mSlider);
	}
	virtual ~ParamBoundSlider()
	{
		mParam->removeListener(this);
	}
	virtual void resized() override
	{
		juce::Rectangle<int> rc = getLocalBounds().reduced(2);
		mLabel.setBounds(rc.removeFromTop(LabelHeight));
		mSlider.setBounds(rc);
		mSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxAbove, false, rc.getWidth(), LabelHeight);
	}
	virtual void paint(juce::Graphics& g) override
	{
		g.fillAll(juce::Colour(0x10000000));
	}
	// juce::AsyncUpdater
	virtual void handleAsyncUpdate() override
	{
		mSlider.setValue(mParam->getValue(), juce::NotificationType::dontSendNotification);
	}
	// juce::AudioProcessorParameter::Listener
	virtual void parameterValueChanged(int, float) override
	{
		triggerAsyncUpdate();
	}
	virtual void parameterGestureChanged(int, bool) override
	{
	}
};

// ================================================================================
// the processor editor

class Vowel2DAudioProcessorEditorImpl
	: public Vowel2DAudioProcessorEditor
{
private:
	Vowel2DAudioProcessor& mProcessor;
	VPlotView mVPlotView;
	FPlotView mFPlotView;
	juce::OwnedArray<ParamBoundSlider> mSliderList;
	enum { Margin = 8, Spacing = 4, FreqSize = 480, AmpSize = 200, KnobSize = 64, LabelHeight = 15, };
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Vowel2DAudioProcessorEditorImpl)
public:
	Vowel2DAudioProcessorEditorImpl(Vowel2DAudioProcessor& p)
		: Vowel2DAudioProcessorEditor(p)
		, mProcessor(p)
		, mVPlotView(&p)
		, mFPlotView(&p)
	{
		setOpaque(true);
		addAndMakeVisible(mVPlotView);
		addAndMakeVisible(mFPlotView);
		const auto& params = mProcessor.getParameters();
		mSliderList.ensureStorageAllocated(params.size());
		for(int c = params.size(), i = 0; i < c; ++i)
		{
			ParamBoundSlider* slider = new ParamBoundSlider(params[i]);
			mSliderList.add(slider);
			addAndMakeVisible(slider);
		}
		setSize(Margin * 2 + FreqSize, Margin * 2 + FreqSize + Spacing + AmpSize + Spacing + LabelHeight * 2 + KnobSize);
	}
	virtual ~Vowel2DAudioProcessorEditorImpl() override
	{
	}
	virtual void resized() override
	{
		juce::Rectangle<int> rc = getLocalBounds().reduced(Margin);
		mVPlotView.setBounds(rc.removeFromTop(FreqSize));
		rc.removeFromTop(Spacing);
		mFPlotView.setBounds(rc.removeFromTop(AmpSize));
		rc.removeFromTop(Spacing);
		for(int c = mSliderList.size(), i = 0; i < c; ++i)
		{
			mSliderList[i]->setBounds(rc.removeFromLeft(KnobSize));
			rc.removeFromLeft(Spacing);
		}
	}
	virtual void paint(juce::Graphics& g) override
	{
		g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
	}
};

Vowel2DAudioProcessorEditor* Vowel2DAudioProcessorEditor::createInstance(Vowel2DAudioProcessor& p)
{
	return new Vowel2DAudioProcessorEditorImpl(p);
}
