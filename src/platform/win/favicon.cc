#include "platform/win/favicon.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <utility>

namespace brouk::win {

namespace {

using Microsoft::WRL::ComPtr;

// Larger "icons" are not decoded: a page could send any image.
constexpr size_t kMaxPngBytes = 1024 * 1024;

}  // namespace

Favicon::~Favicon() {
  if (bitmap_) {
    DeleteObject(bitmap_);
  }
}

Favicon::Favicon(Favicon&& other) noexcept
    : bitmap_(std::exchange(other.bitmap_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

Favicon& Favicon::operator=(Favicon&& other) noexcept {
  if (this != &other) {
    if (bitmap_) {
      DeleteObject(bitmap_);
    }
    bitmap_ = std::exchange(other.bitmap_, nullptr);
    size_ = std::exchange(other.size_, 0);
  }
  return *this;
}

// static
Favicon Favicon::FromPng(const std::vector<std::uint8_t>& png, int size) {
  Favicon icon;
  if (png.empty() || png.size() > kMaxPngBytes || size <= 0) {
    return icon;
  }
  // COM is initialised on the UI thread by Chromium.
  ComPtr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(
          CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
          IID_IWICImagingFactory,
          reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf())))) {
    return icon;
  }
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICBitmapScaler> scaler;
  ComPtr<IWICFormatConverter> converter;
  // WIC only reads the buffer; it takes a non-const pointer.
  BYTE* data = const_cast<BYTE*>(png.data());
  if (FAILED(factory->CreateStream(&stream)) ||
      FAILED(
          stream->InitializeFromMemory(data, static_cast<DWORD>(png.size()))) ||
      FAILED(factory->CreateDecoderFromStream(
          stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
      FAILED(decoder->GetFrame(0, &frame)) ||
      FAILED(factory->CreateBitmapScaler(&scaler)) ||
      FAILED(scaler->Initialize(frame.Get(), static_cast<UINT>(size),
                                static_cast<UINT>(size),
                                WICBitmapInterpolationModeFant)) ||
      FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
    return icon;
  }

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;  // Top-down.
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap =
      CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bitmap) {
    return icon;
  }
  const UINT stride = static_cast<UINT>(size) * 4;
  if (FAILED(converter->CopyPixels(nullptr, stride, stride * size,
                                   static_cast<BYTE*>(bits)))) {
    DeleteObject(bitmap);
    return icon;
  }
  icon.bitmap_ = bitmap;
  icon.size_ = size;
  return icon;
}

void Favicon::Draw(HDC dc, int x, int y) const {
  if (!bitmap_) {
    return;
  }
  HDC source = CreateCompatibleDC(dc);
  HGDIOBJ previous = SelectObject(source, bitmap_);
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  AlphaBlend(dc, x, y, size_, size_, source, 0, 0, size_, size_, blend);
  SelectObject(source, previous);
  DeleteDC(source);
}

}  // namespace brouk::win
