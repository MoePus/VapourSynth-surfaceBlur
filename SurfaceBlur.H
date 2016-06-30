#ifndef _LC_H_
#define _LC_H_

#include "Helper.h"
#include <iostream>
#include <vector>  
#include <algorithm> 
#include "config.h"
#include <functional>
#ifdef __AVX__OPT__
#include "immintrin.h"
#endif
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned int   DWORD;

template<typename pointType>
class SB_Process
{
private:
	VSFrameContext *frameCtx = nullptr;
	VSCore *core = nullptr;
	const VSAPI *vsapi = nullptr;
	const SB_BASIC_DATA &d;

	const VSFrameRef *src = nullptr;
	VSFrameRef *dst = nullptr;
	const VSFormat *frameFormat = nullptr;
	int dst_stride[3];
	int src_stride[3];
	int dst_height[3];
	int dst_width[3];
	int dst_pcount[3];

	int height, width;
	int planes;

	int frame;
	constexpr static float levelRange = 1 << (sizeof(pointType) * 8);
	constexpr static float levelMiddle = static_cast<float>((static_cast<uint8_t>(~0)) >> 1);
	template < typename funcType >
	void LoopPoint(int width, int height, int stride, funcType func)
	{
		for (int y = 0; y < height; ++y)
		{
			int lp = y * stride;

			for (const int upper = lp + width; lp < upper; ++lp)
			{
				func(lp);
			}
		}
	}
	template < typename funcType >
	void LoopLine(int width, int height, int stride, funcType func)
	{
		for (int y = 0; y < height; ++y)
		{
			int lp = y * stride;
			func(lp, lp + width);
		}
	}

public:
	SB_Process(const SB_BASIC_DATA &_d, int n, VSFrameContext *_frameCtx, VSCore *_core, const VSAPI *_vsapi)
		:core(_core), frameCtx(_frameCtx), vsapi(_vsapi), d(_d) 
	{
		frame = n;
		src = vsapi->getFrameFilter(n, d.node, frameCtx);
		frameFormat = vsapi->getFrameFormat(src);

		height = vsapi->getFrameHeight(src, 0);
		width = vsapi->getFrameWidth(src, 0);
		planes = frameFormat->numPlanes;

		dst = vsapi->newVideoFrame(frameFormat, width, height, src, core);
		
		for (int i = 0; i < planes; ++i)
		{
			dst_height[i] = vsapi->getFrameHeight(dst, i);
			dst_width[i] = vsapi->getFrameWidth(dst, i);
			dst_stride[i] = vsapi->getStride(dst, i) / frameFormat->bytesPerSample;
			src_stride[i] = vsapi->getStride(src, i) / frameFormat->bytesPerSample;
			dst_pcount[i] = dst_height[i] * dst_stride[i];
		}
	};

	~SB_Process()
	{
		vsapi->freeFrame(src);
	}
	VSFrameRef *GetDst()
	{
		return dst;
	}

	void process()
	{
		float threshold = d.getThreshold() * 2.5f;
		int radius = d.getRadius();
		for (int i = 0; i < planes; i++)
		{
			const pointType * srcPlane = reinterpret_cast<pointType const*>(vsapi->getReadPtr(src,i));
			pointType * dstPlane = reinterpret_cast<pointType *>(vsapi->getWritePtr(dst, i));

			const auto getPixel = [&](int x, int y)-> pointType
			{
				return srcPlane[y*src_stride[i] + x];
			};

			const auto absminus = [](float a , float b)-> float
			{
				return a > b ? a - b : b - a;
			};

			const auto clamp = [this](float a) ->pointType
			{
				if (a >= levelRange)
					a = levelRange - 1;
				return static_cast<pointType>(a);
			};

			LoopPoint(dst_width[i], dst_height[i], dst_stride[i], [&](int lp)
			{
				int x = lp % dst_stride[i];
				int y = lp / dst_stride[i];
				int LUX = x - radius;
				int LUY = y - radius;
				const int r = radius * 2 + 1;
				const int width = dst_width[i];
				const int height = dst_height[i];
				const int widthm = width - 1;
				const int heightm = height - 1;
				const auto getSubPixel0 = [&](int ord)-> pointType
				{
					int ordx = LUX + ord % r;
					int ordy = LUY + ord / r;
					ordx > 0 ? (ordx <= widthm ? ordx : ordx = widthm) : ordx = 0;
					ordy > 0 ? (ordy <= heightm ? ordy : ordy = heightm) : ordy = 0;
					return srcPlane[ordy*src_stride[i] + ordx];
				};
				const auto getSubPixel1 = [&](int ord)-> pointType
				{
					int ordx = LUX + ord % r;
					int ordy = LUY + ord / r;
					return srcPlane[ordy*src_stride[i] + ordx];
				};
				
				bool testFlag = 1;
				if (LUX >= 0 && x + radius < width&&LUY >= 0 && y + radius < height)
				{
					testFlag = 0;
				}

				pointType xo = getPixel(x, y);
				float xdu = 0, xdd = 0;
				int SIMD_PROCESSED = 0;
				const int processArea = r*r;
#ifdef __AVX__OPT__
				SIMD_PROCESSED = processArea - (processArea % 8);
				const __m256 mm0 = _mm256_set1_ps(1.0f);
				const __m256 mm1 = _mm256_set1_ps(threshold);
				const __m256 mm2 = _mm256_set1_ps(xo);
				const __m256 mm3 = _mm256_set1_ps(0.0f);
				__m256 weightSum = _mm256_set1_ps(0.0f);
				__m256 weightedSum = _mm256_set1_ps(0.0f);
				if (testFlag)
				{
					for (int j = 0; j < SIMD_PROCESSED; j += 8)
					{
						const __m256 mm4 = _mm256_set_ps(
							getSubPixel0(j), getSubPixel0(j + 1),
							getSubPixel0(j + 2), getSubPixel0(j + 3),
							getSubPixel0(j + 4), getSubPixel0(j + 5),
							getSubPixel0(j + 6), getSubPixel0(j + 7)
						);

						__m256 diff = _mm256_div_ps(_mm256_sub_ps(mm2, mm4), mm1);
						__m256 diffNeg = _mm256_sub_ps(mm3, diff);
						__m256 diffAbs = _mm256_add_ps(
							_mm256_max_ps(mm3, diff),
							_mm256_max_ps(mm3, diffNeg)
						);
						const __m256 weight = _mm256_max_ps(mm3, _mm256_sub_ps(mm0, diffAbs));
						const __m256 weighted = _mm256_mul_ps(weight, mm4);
						weightSum = _mm256_add_ps(weightSum, weight);
						weightedSum = _mm256_add_ps(weightedSum, weighted);
					}
				}
				else
				{
					for (int j = 0; j < SIMD_PROCESSED; j += 8)
					{
						const __m256 mm4 = _mm256_set_ps(
							getSubPixel1(j), getSubPixel1(j + 1),
							getSubPixel1(j + 2), getSubPixel1(j + 3),
							getSubPixel1(j + 4), getSubPixel1(j + 5),
							getSubPixel1(j + 6), getSubPixel1(j + 7)
						);

						__m256 diff = _mm256_div_ps(_mm256_sub_ps(mm2, mm4), mm1);
						__m256 diffNeg = _mm256_sub_ps(mm3, diff);
						__m256 diffAbs = _mm256_add_ps(
							_mm256_max_ps(mm3, diff),
							_mm256_max_ps(mm3, diffNeg)
						);
						const __m256 weight = _mm256_max_ps(mm3, _mm256_sub_ps(mm0, diffAbs));
						const __m256 weighted = _mm256_mul_ps(weight, mm4);
						weightSum = _mm256_add_ps(weightSum, weight);
						weightedSum = _mm256_add_ps(weightedSum, weighted);
					}
				}
				alignas(32) float fw0[8], fw1[8];
				_mm256_store_ps(fw0, weightSum);
				_mm256_store_ps(fw1, weightedSum);
				xdd += fw0[0] + fw0[1] + fw0[2] + fw0[3] + fw0[4] + fw0[5] + fw0[6] + fw0[7];
				xdu += fw1[0] + fw1[1] + fw1[2] + fw1[3] + fw1[4] + fw1[5] + fw1[6] + fw1[7];
#endif
				for (int j = SIMD_PROCESSED; j < processArea; j++)
				{
					const pointType xi = getSubPixel0(j);
					const float b = (1.0f - absminus(xi,xo) / threshold);
					if (b > 0)
					{
						xdu += b * xi;
						xdd += b;
					}
				}
				if (xdd != 0)
					dstPlane[lp] = clamp(xdu / xdd);
				else
					dstPlane[lp] = srcPlane[lp];
			});
		}
	}
};

#endif