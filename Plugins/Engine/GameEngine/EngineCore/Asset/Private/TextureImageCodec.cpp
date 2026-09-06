#include "TextureImageCodec.h"

#include <Log.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <objbase.h>
#	include <wincodec.h>
#	include <wrl/client.h>
#	pragma comment(lib, "windowscodecs.lib")
#	pragma comment(lib, "ole32.lib")
#endif

namespace Maho
{
namespace Resource
{
namespace TextureImageCodec
{
namespace
{

#if defined(_WIN32)
[[nodiscard]] bool DecodeRasterWic(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	FDecodedImage& Out)
{
	using Microsoft::WRL::ComPtr;

	HRESULT Hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool bNeedUninit = SUCCEEDED(Hr);
	if (FAILED(Hr) && Hr != RPC_E_CHANGED_MODE)
	{
		MAHO_LOG_CORE_ERROR("TextureImageCodec: CoInitializeEx failed ({})", static_cast<long>(Hr));
		return false;
	}

	ComPtr<IWICImagingFactory> Factory;
	Hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(Factory.GetAddressOf()));
	if (FAILED(Hr))
	{
		MAHO_LOG_CORE_ERROR("TextureImageCodec: WIC factory create failed");
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	ComPtr<IWICStream> Stream;
	Hr = Factory->CreateStream(Stream.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	Hr = Stream->InitializeFromMemory(
		const_cast<BYTE*>(reinterpret_cast<const BYTE*>(Bytes)),
		static_cast<DWORD>(ByteCount));
	if (FAILED(Hr))
	{
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	ComPtr<IWICBitmapDecoder> Decoder;
	Hr = Factory->CreateDecoderFromStream(
		Stream.Get(),
		nullptr,
		WICDecodeMetadataCacheOnDemand,
		Decoder.GetAddressOf());
	if (FAILED(Hr))
	{
		MAHO_LOG_CORE_ERROR("TextureImageCodec: WIC CreateDecoderFromStream failed ({})", static_cast<long>(Hr));
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	ComPtr<IWICBitmapFrameDecode> Frame;
	Hr = Decoder->GetFrame(0, Frame.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	ComPtr<IWICFormatConverter> Converter;
	Hr = Factory->CreateFormatConverter(Converter.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	Hr = Converter->Initialize(
		Frame.Get(),
		GUID_WICPixelFormat32bppRGBA,
		WICBitmapDitherTypeNone,
		nullptr,
		0.0,
		WICBitmapPaletteTypeCustom);
	if (FAILED(Hr))
	{
		MAHO_LOG_CORE_ERROR("TextureImageCodec: WIC FormatConverter Initialize failed");
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	UINT Width = 0;
	UINT Height = 0;
	Hr = Converter->GetSize(&Width, &Height);
	if (FAILED(Hr) || Width == 0 || Height == 0)
	{
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	const std::size_t Stride = static_cast<std::size_t>(Width) * 4u;
	const std::size_t ByteSize = Stride * static_cast<std::size_t>(Height);
	std::vector<std::uint8_t> Pixels(ByteSize);
	Hr = Converter->CopyPixels(nullptr, static_cast<UINT>(Stride), static_cast<UINT>(ByteSize), Pixels.data());
	if (FAILED(Hr))
	{
		MAHO_LOG_CORE_ERROR("TextureImageCodec: WIC CopyPixels failed");
		if (bNeedUninit) { CoUninitialize(); }
		return false;
	}

	Out.Dimension = ETextureDimension::Tex2D;
	Out.Format = ETexturePixelFormat::RGBA8;
	Out.Width = Width;
	Out.Height = Height;
	Out.Depth = 1;
	Out.ArrayLayers = 1;
	Out.MipCount = 1;
	Out.bSRGB = true;
	Out.Pixels = std::move(Pixels);

	if (bNeedUninit) { CoUninitialize(); }
	return true;
}
#endif // _WIN32

} // namespace

bool DecodeFromMemory(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePath,
	FDecodedImage& Out)
{
	(void)SourcePath;
	if (Bytes == nullptr || ByteCount == 0)
	{
		return false;
	}
#if defined(_WIN32)
	return DecodeRasterWic(Bytes, ByteCount, Out);
#else
	(void)Out;
	return false;
#endif
}

std::string GetExtensionLower(std::string_view Path)
{
	const std::size_t Dot = Path.find_last_of('.');
	if (Dot == std::string_view::npos)
	{
		return {};
	}
	std::string Ext(Path.substr(Dot));
	std::transform(Ext.begin(), Ext.end(), Ext.begin(),
		[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return Ext;
}

bool IsRasterExtension(std::string_view Ext)
{
	return Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg"
		|| Ext == ".bmp" || Ext == ".tif" || Ext == ".tiff"
		|| Ext == ".gif" || Ext == ".ico";
}

} // namespace TextureImageCodec
} // namespace Resource
} // namespace Maho
