#include "platform/win/qr_reader.h"

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <ReadBarcode.h>

#include <utility>

namespace brouk::win {

namespace {

using Microsoft::WRL::ComPtr;

// A page screenshot is a few MB at most; more is not one.
constexpr std::size_t kMaxPngBytes = 64 * 1024 * 1024;
constexpr UINT kMaxPixels = 64 * 1024 * 1024;

// The image as 8-bit grey pixels, rows of `width` bytes; empty if it
// cannot be decoded.
std::vector<std::uint8_t> GreyPixels(std::span<const std::uint8_t> png,
                                     UINT& width,
                                     UINT& height) {
  std::vector<std::uint8_t> pixels;
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  // WIC only reads the buffer; it takes a non-const pointer.
  BYTE* data = const_cast<BYTE*>(png.data());
  if (FAILED(CoCreateInstance(
          CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
          IID_IWICImagingFactory,
          reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf()))) ||
      FAILED(factory->CreateStream(&stream)) ||
      FAILED(
          stream->InitializeFromMemory(data, static_cast<DWORD>(png.size()))) ||
      FAILED(factory->CreateDecoderFromStream(
          stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) ||
      FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat8bppGray,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom)) ||
      FAILED(converter->GetSize(&width, &height)) || width == 0 ||
      height == 0 || width > kMaxPixels / height) {
    return pixels;
  }
  pixels.resize(static_cast<std::size_t>(width) * height);
  if (FAILED(converter->CopyPixels(
          nullptr, width, static_cast<UINT>(pixels.size()), pixels.data()))) {
    pixels.clear();
  }
  return pixels;
}

}  // namespace

std::vector<std::string> ReadQrCodes(std::span<const std::uint8_t> png) {
  std::vector<std::string> texts;
  if (png.empty() || png.size() > kMaxPngBytes) {
    return texts;
  }
  // COM for WIC; undone only if this call started it.
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  UINT width = 0;
  UINT height = 0;
  std::vector<std::uint8_t> pixels = GreyPixels(png, width, height);
  if (SUCCEEDED(com)) {
    CoUninitialize();
  }
  if (pixels.empty()) {
    return texts;
  }
  // zxing-cpp may throw (allocation, unexpected data); nothing leaves here.
  try {
    const ZXing::ImageView image(pixels.data(), static_cast<int>(width),
                                 static_cast<int>(height),
                                 ZXing::ImageFormat::Lum);
    const ZXing::ReaderOptions options =
        ZXing::ReaderOptions().setFormats(ZXing::BarcodeFormat::QRCode);
    for (const ZXing::Barcode& code : ZXing::ReadBarcodes(image, options)) {
      if (code.isValid() && !code.text().empty()) {
        texts.push_back(code.text());
      }
    }
  } catch (...) {
    texts.clear();
  }
  return texts;
}

}  // namespace brouk::win
