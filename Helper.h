#ifndef _LC_Hepler_
#define _LC_Hepler_

#include <vapoursynth/VapourSynth.h>
#include <vapoursynth/VSHelper.h>
#include <iostream>
#include <string>
class SB_BASIC_DATA
{
public:
	VSNodeRef *node;
	const VSVideoInfo *vi = nullptr;
	const VSAPI *vsapi = nullptr;
	void setError(VSMap *out, std::string error)
	{
		vsapi->setError(out, error.c_str());
	}
	void setParams(int a, double b)
	{
		radius = a;
		threshold = static_cast<float>(b);
	}

	int getRadius() const
	{
		return radius;
	}
	float getThreshold() const
	{
		return threshold;
	}
private:
	int radius;
	float threshold;
};

static void VS_CC filterInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	SB_BASIC_DATA *d = (SB_BASIC_DATA *)* instanceData;
	vsapi->setVideoInfo(d->vi, 1, node);
}

static const VSFrameRef *VS_CC filterGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);

static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	SB_BASIC_DATA *d = (SB_BASIC_DATA *)instanceData;
	vsapi->freeNode(d->node);
	delete d;
}

static void VS_CC LC_Create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
	SB_BASIC_DATA *data = new SB_BASIC_DATA;
	data->vsapi = vsapi;
	data->node = vsapi->propGetNode(in, "input", 0, 0);
	data->vi = vsapi->getVideoInfo(data->node);

	if (!isConstantFormat(data->vi))
	{
		data->setError(out, "Invalid input clip, only constant format input supported");
		return;
	}
	else if ((data->vi->format->colorFamily != cmYUV 
		&& data->vi->format->colorFamily != cmGray
		&& data->vi->format->colorFamily != cmRGB
		) || (data->vi->format->bitsPerSample!=8 && data->vi->format->bitsPerSample != 16))
	{
		data->setError(out, "Invalid input clip, only Gray, YUV or RGB and P8 or P16 input supported");
		return;
	}

	int error = 0;
	int radius = static_cast<int>(vsapi->propGetInt(in, "radius", 0, &error));
	if (error)
	{
		radius = 2;
	}
	
	double threshold = vsapi->propGetFloat(in, "threshold", 0, &error);
	if (error)
	{
		threshold = 5.5f;
	}

	data->setParams(radius, threshold);

	vsapi->createFilter(in, out, "Filter", filterInit, filterGetFrame, filterFree, fmParallel, 0, data, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) 
{
	configFunc("com.vapoursynth.SB", "surfaceblur", "surface blur", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("surfaceblur",
		"input:clip;"
		"threshold:float:opt;"
		"radius:int:opt;",
		LC_Create, 0, plugin);
}

#endif
