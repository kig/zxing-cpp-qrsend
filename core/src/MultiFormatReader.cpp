/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
*/
// SPDX-License-Identifier: Apache-2.0

#include "MultiFormatReader.h"

#include "BarcodeFormat.h"
#include "BinaryBitmap.h"
#include "Reader.h"
#include "ReaderOptions.h"
#include "Version.h"

#if ZXING_ENABLE_AZTEC
#include "aztec/AZReader.h"
#endif
#if ZXING_ENABLE_DATAMATRIX
#include "datamatrix/DMReader.h"
#endif
#if ZXING_ENABLE_MAXICODE
#include "maxicode/MCReader.h"
#endif
#if ZXING_ENABLE_1D
#include "oned/ODReader.h"
#endif
#if ZXING_ENABLE_PDF417
#include "pdf417/PDFReader.h"
#endif
#if ZXING_ENABLE_QRCODE
#include "qrcode/QRReader.h"
#endif

#include <cstdio>
#include <memory>

#ifndef ZXING_QR_DEBUG_LOG
#define ZXING_QR_DEBUG_LOG 0
#endif

#if ZXING_QR_DEBUG_LOG
#define LOG_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

namespace ZXing {

MultiFormatReader::MultiFormatReader(const ReaderOptions& opts) : _opts(opts)
{
	using enum BarcodeFormat;

	// Put linear readers upfront in "normal" mode
#if ZXING_ENABLE_1D
	if (!opts.tryHarder() && opts.hasAnyFormat(AllLinear))
		_readers.emplace_back(new OneD::Reader(opts));
#endif

#if ZXING_ENABLE_QRCODE
	if (opts.hasAnyFormat(QRCode))
		_readers.emplace_back(new QRCode::Reader(opts, true));
#endif
#if ZXING_ENABLE_DATAMATRIX
	if (opts.hasAnyFormat(DataMatrix))
		_readers.emplace_back(new DataMatrix::Reader(opts, true));
#endif
#if ZXING_ENABLE_AZTEC
	if (opts.hasAnyFormat(Aztec))
		_readers.emplace_back(new Aztec::Reader(opts, true));
#endif
#if ZXING_ENABLE_PDF417
	if (opts.hasAnyFormat(PDF417))
		_readers.emplace_back(new Pdf417::Reader(opts));
#endif
#if ZXING_ENABLE_MAXICODE
	if (opts.hasAnyFormat(MaxiCode))
		_readers.emplace_back(new MaxiCode::Reader(opts));
#endif

	// At end in "try harder" mode
#if ZXING_ENABLE_1D
	if (opts.tryHarder() && opts.hasAnyFormat(AllLinear))
		_readers.emplace_back(new OneD::Reader(opts));
#endif
}

MultiFormatReader::~MultiFormatReader() = default;

Barcodes MultiFormatReader::read(const BinaryBitmap& image, int maxSymbols) const
{
	LOG_DEBUG( "[zx-trace] MultiFormatReader::read readers=%zu maxSymbols=%d inverted=%d\n",
			_readers.size(), maxSymbols, image.inverted());
	Barcodes res;

	for (const auto& reader : _readers) {
		if (image.inverted() && !reader->supportsInversion)
			continue;
		LOG_DEBUG( "[zx-trace] reader->read enter supportsInv=%d\n", reader->supportsInversion);
		auto r = reader->read(image, maxSymbols);
		LOG_DEBUG( "[zx-trace] reader->read exit n=%zu\n", r.size());
		if (!_opts.returnErrors())
			std::erase_if(r, [](auto&& s) { return !s.isValid(); });
		maxSymbols -= Size(r);
		res.insert(res.end(), std::move_iterator(r.begin()), std::move_iterator(r.end()));
		if (maxSymbols <= 0)
			break;
	}
	LOG_DEBUG( "[zx-trace] MultiFormatReader::read exit total=%zu\n", res.size());

	// sort barcodes based on their position on the image
	std::sort(res.begin(), res.end(), [](const Barcode& l, const Barcode& r) {
		auto lp = l.position().topLeft();
		auto rp = r.position().topLeft();
		return lp.y < rp.y || (lp.y == rp.y && lp.x < rp.x);
	});

	return res;
}

} // ZXing
