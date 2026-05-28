/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
* Copyright 2022 Axel Waggershauser
*/
// SPDX-License-Identifier: Apache-2.0

#include "QRReader.h"

#include "BarcodeData.h"
#include "BinaryBitmap.h"
#include "ConcentricFinder.h"
#include "DecoderResult.h"
#include "DetectorResult.h"
#include "LogMatrix.h"
#include "QRDecoder.h"
#include "QRDetector.h"
#include "ReaderOptions.h"

#include <cmath>
#include <cstdio>
#include <utility>

#ifndef ZXING_QR_DEBUG_LOG
#define ZXING_QR_DEBUG_LOG 0
#endif

#if ZXING_QR_DEBUG_LOG
#define LOG_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

namespace ZXing::QRCode {

static BarcodeData readPure(const BitMatrix* binImg, const ReaderOptions& _opts)
{
	DetectorResult detectorResult;
	if (_opts.hasFormat(BarcodeFormat::QRCode))
		detectorResult = DetectPureQR(*binImg);
	if (_opts.hasFormat(BarcodeFormat::MicroQRCode) && !detectorResult.isValid())
		detectorResult = DetectPureMQR(*binImg);
	if (_opts.hasFormat(BarcodeFormat::RMQRCode) && !detectorResult.isValid())
		detectorResult = DetectPureRMQR(*binImg);

	if (!detectorResult.isValid())
		return {};

	auto decoderResult = Decode(detectorResult.bits());
	auto format = detectorResult.bits().width() != detectorResult.bits().height() ? BarcodeFormat::RMQRCode
				  : detectorResult.bits().width() < 21                            ? BarcodeFormat::MicroQRCode
																				  : BarcodeFormat::QRCode;

	return MatrixBarcode(std::move(decoderResult), std::move(detectorResult), format);
}

void logFPSet(const FinderPatternSet& fps [[maybe_unused]])
{
#ifdef PRINT_DEBUG
	auto drawLine = [](PointF a, PointF b) {
		int steps = maxAbsComponent(b - a);
		PointF dir = bresenhamDirection(PointF(b - a));
		for (int i = 0; i < steps; ++i)
			log(a + i * dir, 2);
	};

	drawLine(fps.bl, fps.tl);
	drawLine(fps.tl, fps.tr);
	drawLine(fps.tr, fps.bl);
#endif
}

BarcodesData Reader::read(const BinaryBitmap& image, int maxSymbols) const
{
	auto binImg = image.getBitMatrix();
	if (binImg == nullptr)
		return {};
	LOG_DEBUG( "[zx-trace] QRReader::read enter w=%d h=%d maxSymbols=%d\n",
			binImg->width(), binImg->height(), maxSymbols);

#ifdef PRINT_DEBUG
	LogMatrixWriter lmw(log, *binImg, 5, "qr-log.pnm");
#endif

	if (_opts.isPure())
		return ToVector(readPure(binImg, _opts));

	LOG_DEBUG( "[zx-trace] QRReader::FindFinderPatterns enter\n");
	auto allFPs = FindFinderPatterns(*binImg, _opts.tryHarder());
	LOG_DEBUG( "[zx-trace] QRReader::FindFinderPatterns exit n=%zu\n", allFPs.size());

#ifdef PRINT_DEBUG
	printf("allFPs: %d\n", Size(allFPs));
#endif

	std::vector<ConcentricPattern> usedFPs;
	BarcodesData res;
	
	if (_opts.hasFormat(BarcodeFormat::QRCodeModel1 | BarcodeFormat::QRCodeModel2)) {
		LOG_DEBUG( "[zx-trace] QRReader::GenerateFinderPatternSets enter\n");
		auto allFPSets = GenerateFinderPatternSets(allFPs);
		LOG_DEBUG( "[zx-trace] QRReader::GenerateFinderPatternSets exit n=%zu\n", allFPSets.size());
		const int imgW = binImg->width();
		const int imgH = binImg->height();
		size_t fpSetIndex = 0;
		for (const auto& fpSet : allFPSets) {
			const size_t currentIndex = ++fpSetIndex;
			auto isFinitePoint = [](const PointF& p) { return std::isfinite(p.x) && std::isfinite(p.y); };
			auto isInBounds = [imgW, imgH](const PointF& p) {
				return p.x >= 0 && p.y >= 0 && p.x < imgW && p.y < imgH;
			};
			if (!isFinitePoint(fpSet.bl) || !isFinitePoint(fpSet.tl) || !isFinitePoint(fpSet.tr)) {
				LOG_DEBUG( "[zx-trace] QRReader::fpSet[%zu/%zu] skip non-finite\n", currentIndex, allFPSets.size());
				continue;
			}
			if (!isInBounds(fpSet.bl) || !isInBounds(fpSet.tl) || !isInBounds(fpSet.tr)) {
				LOG_DEBUG( "[zx-trace] QRReader::fpSet[%zu/%zu] skip out-of-bounds\n", currentIndex, allFPSets.size());
				continue;
			}
			if (fpSet.bl.size <= 0 || fpSet.tl.size <= 0 || fpSet.tr.size <= 0 ||
				fpSet.bl.size > std::max(imgW, imgH) || fpSet.tl.size > std::max(imgW, imgH) || fpSet.tr.size > std::max(imgW, imgH)) {
				LOG_DEBUG( "[zx-trace] QRReader::fpSet[%zu/%zu] skip invalid-size bl=%d tl=%d tr=%d\n",
						currentIndex, allFPSets.size(), fpSet.bl.size, fpSet.tl.size, fpSet.tr.size);
				continue;
			}
			if (Contains(usedFPs, fpSet.bl) || Contains(usedFPs, fpSet.tl) || Contains(usedFPs, fpSet.tr))
				continue;

			LOG_DEBUG( "[zx-trace] QRReader::fpSet[%zu/%zu] sample enter\n", currentIndex, allFPSets.size());
			logFPSet(fpSet);

			for (auto&& detectorResult: SampleQR(*binImg, fpSet)) {
				LOG_DEBUG( "[zx-trace] QRReader::fpSet[%zu/%zu] decode enter\n", currentIndex, allFPSets.size());
				auto decoderResult = Decode(detectorResult.bits());
				LOG_DEBUG( "[zx-trace] QRReader::fpSet[%zu/%zu] decode exit valid=%d\n",
						currentIndex, allFPSets.size(), decoderResult.isValid());
				if ((decoderResult.content().symbology.modifier == '0' && !_opts.hasFormat(BarcodeFormat::QRCodeModel1))
					|| (decoderResult.content().symbology.modifier == '1' && !_opts.hasFormat(BarcodeFormat::QRCodeModel2)))
					continue;
				if (decoderResult.isValid()) {
					usedFPs.push_back(fpSet.bl);
					usedFPs.push_back(fpSet.tl);
					usedFPs.push_back(fpSet.tr);
				}
				if (decoderResult.isValid(_opts.returnErrors())) {
					res.emplace_back(MatrixBarcode(std::move(decoderResult), std::move(detectorResult), BarcodeFormat::QRCode));
					// if we found a valid symbol, we stop the inner loop
					if (res.back().isValid() || (maxSymbols && Size(res) == maxSymbols))
						break;
				}
			}
			if (maxSymbols && Size(res) == maxSymbols)
				break;
		}
	}
	
	if (_opts.hasFormat(BarcodeFormat::MicroQRCode) && !(maxSymbols && Size(res) == maxSymbols)) {
		for (const auto& fp : allFPs) {
			if (Contains(usedFPs, fp))
				continue;

			auto detectorResult = SampleMQR(*binImg, fp);
			if (detectorResult.isValid()) {
				auto decoderResult = Decode(detectorResult.bits());
				if (decoderResult.isValid(_opts.returnErrors())) {
					res.emplace_back(MatrixBarcode(std::move(decoderResult), std::move(detectorResult), BarcodeFormat::MicroQRCode));
					if (maxSymbols && Size(res) == maxSymbols)
						break;
				}

			}
		}
	}
	
	if (_opts.hasFormat(BarcodeFormat::RMQRCode) && !(maxSymbols && Size(res) == maxSymbols)) {
		// TODO proper
		for (const auto& fp : allFPs) {
			if (Contains(usedFPs, fp))
				continue;

			auto detectorResult = SampleRMQR(*binImg, fp);
			if (detectorResult.isValid()) {
				auto decoderResult = Decode(detectorResult.bits());
				if (decoderResult.isValid(_opts.returnErrors())) {
					res.emplace_back(MatrixBarcode(std::move(decoderResult), std::move(detectorResult), BarcodeFormat::RMQRCode));
					if (maxSymbols && Size(res) == maxSymbols)
						break;
				}

			}
		}
	}
	LOG_DEBUG( "[zx-trace] QRReader::read exit n=%zu\n", res.size());

	return res;
}

} // namespace ZXing::QRCode
