#include "TextureImageCodec.h"

#include <Core/Misc/Log.h>
#include <Core/Misc/Utf8Path.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

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

#if defined(MAHO_WITH_LIBKTX)
#	include <ktx.h>
#endif

namespace Maho
{
namespace TextureImageCodec
{
namespace
{

[[nodiscard]] bool ContainsInsensitive(std::string_view Hay, std::string_view Needle)
{
	if (Needle.empty() || Hay.size() < Needle.size())
	{
		return false;
	}
	auto ToLower = [](char C) -> char
	{
		return (C >= 'A' && C <= 'Z') ? static_cast<char>(C - 'A' + 'a') : C;
	};
	for (std::size_t I = 0; I + Needle.size() <= Hay.size(); ++I)
	{
		bool bMatch = true;
		for (std::size_t J = 0; J < Needle.size(); ++J)
		{
			if (ToLower(Hay[I + J]) != ToLower(Needle[J]))
			{
				bMatch = false;
				break;
			}
		}
		if (bMatch)
		{
			return true;
		}
	}
	return false;
}

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
		MAHO_CORE_ERROR("TextureImageCodec: CoInitializeEx failed ({})", static_cast<long>(Hr));
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
		MAHO_CORE_ERROR("TextureImageCodec: WIC factory create failed");
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IWICStream> Stream;
	Hr = Factory->CreateStream(Stream.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	Hr = Stream->InitializeFromMemory(
		const_cast<BYTE*>(reinterpret_cast<const BYTE*>(Bytes)),
		static_cast<DWORD>(ByteCount));
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
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
		MAHO_CORE_ERROR("TextureImageCodec: WIC CreateDecoderFromStream failed ({})", static_cast<long>(Hr));
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IWICBitmapFrameDecode> Frame;
	Hr = Decoder->GetFrame(0, Frame.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IWICFormatConverter> Converter;
	Hr = Factory->CreateFormatConverter(Converter.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
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
		MAHO_CORE_ERROR("TextureImageCodec: WIC FormatConverter Initialize failed");
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	UINT Width = 0;
	UINT Height = 0;
	Hr = Converter->GetSize(&Width, &Height);
	if (FAILED(Hr) || Width == 0 || Height == 0)
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	const std::size_t Stride = static_cast<std::size_t>(Width) * 4u;
	const std::size_t ByteSize = Stride * static_cast<std::size_t>(Height);
	std::vector<std::uint8_t> Pixels(ByteSize);
	Hr = Converter->CopyPixels(nullptr, static_cast<UINT>(Stride), static_cast<UINT>(ByteSize), Pixels.data());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
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

	if (bNeedUninit)
	{
		CoUninitialize();
	}
	return true;
}

[[nodiscard]] bool EncodeRasterWic(const FTexture& Texture, const std::string& DestinationPath)
{
	if (Texture.GetPixelFormat() != ETexturePixelFormat::RGBA8
		|| Texture.GetPixels().empty()
		|| Texture.GetWidth() == 0
		|| Texture.GetHeight() == 0)
	{
		MAHO_CORE_ERROR("TextureImageCodec: WIC encode requires RGBA8 CPU pixels");
		return false;
	}

	using Microsoft::WRL::ComPtr;
	HRESULT Hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool bNeedUninit = SUCCEEDED(Hr);
	if (FAILED(Hr) && Hr != RPC_E_CHANGED_MODE)
	{
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
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	const std::string Ext = GetExtensionLower(DestinationPath);
	GUID Container = GUID_ContainerFormatPng;
	if (Ext == "jpg" || Ext == "jpeg")
	{
		Container = GUID_ContainerFormatJpeg;
	}
	else if (Ext == "bmp")
	{
		Container = GUID_ContainerFormatBmp;
	}
	else if (Ext == "tif" || Ext == "tiff")
	{
		Container = GUID_ContainerFormatTiff;
	}

	const std::wstring WidePath = Utf8ToWide(DestinationPath);
	ComPtr<IWICStream> Stream;
	Hr = Factory->CreateStream(Stream.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}
	Hr = Stream->InitializeFromFilename(WidePath.c_str(), GENERIC_WRITE);
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IWICBitmapEncoder> Encoder;
	Hr = Factory->CreateEncoder(Container, nullptr, Encoder.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}
	Hr = Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache);
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IWICBitmapFrameEncode> Frame;
	ComPtr<IPropertyBag2> Props;
	Hr = Encoder->CreateNewFrame(Frame.GetAddressOf(), Props.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}
	Hr = Frame->Initialize(Props.Get());
	const UINT W = Texture.GetWidth();
	const UINT H = Texture.GetHeight();
	Hr = Frame->SetSize(W, H);
	WICPixelFormatGUID Pf = GUID_WICPixelFormat32bppRGBA;
	Hr = Frame->SetPixelFormat(&Pf);
	const UINT Stride = W * 4u;
	Hr = Frame->WritePixels(H, Stride, Stride * H, const_cast<BYTE*>(Texture.GetPixels().data()));
	Hr = Frame->Commit();
	Hr = Encoder->Commit();

	if (bNeedUninit)
	{
		CoUninitialize();
	}
	return SUCCEEDED(Hr);
}

[[nodiscard]] bool EncodeRasterWicToMemory(
	const FTexture& Texture,
	REFGUID ContainerFormat,
	std::vector<std::uint8_t>& OutBytes)
{
	OutBytes.clear();
	if (Texture.GetPixelFormat() != ETexturePixelFormat::RGBA8
		|| Texture.GetPixels().empty()
		|| Texture.GetWidth() == 0
		|| Texture.GetHeight() == 0)
	{
		return false;
	}

	using Microsoft::WRL::ComPtr;
	HRESULT Hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool bNeedUninit = SUCCEEDED(Hr);
	if (FAILED(Hr) && Hr != RPC_E_CHANGED_MODE)
	{
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
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IStream> MemoryStream;
	Hr = CreateStreamOnHGlobal(nullptr, TRUE, MemoryStream.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IWICStream> Stream;
	Hr = Factory->CreateStream(Stream.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}
	Hr = Stream->InitializeFromIStream(MemoryStream.Get());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IWICBitmapEncoder> Encoder;
	Hr = Factory->CreateEncoder(ContainerFormat, nullptr, Encoder.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}
	Hr = Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache);
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	ComPtr<IWICBitmapFrameEncode> Frame;
	ComPtr<IPropertyBag2> Props;
	Hr = Encoder->CreateNewFrame(Frame.GetAddressOf(), Props.GetAddressOf());
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}
	Hr = Frame->Initialize(Props.Get());
	const UINT W = Texture.GetWidth();
	const UINT H = Texture.GetHeight();
	Hr = Frame->SetSize(W, H);
	WICPixelFormatGUID Pf = GUID_WICPixelFormat32bppRGBA;
	Hr = Frame->SetPixelFormat(&Pf);
	const UINT Stride = W * 4u;
	Hr = Frame->WritePixels(H, Stride, Stride * H, const_cast<BYTE*>(Texture.GetPixels().data()));
	Hr = Frame->Commit();
	Hr = Encoder->Commit();
	if (FAILED(Hr))
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	STATSTG Stat{};
	Hr = MemoryStream->Stat(&Stat, STATFLAG_NONAME);
	if (FAILED(Hr) || Stat.cbSize.QuadPart <= 0 || Stat.cbSize.QuadPart > 0x7fffffff)
	{
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	const ULONG Size = static_cast<ULONG>(Stat.cbSize.QuadPart);
	OutBytes.resize(Size);
	LARGE_INTEGER Zero{};
	Hr = MemoryStream->Seek(Zero, STREAM_SEEK_SET, nullptr);
	ULONG Read = 0;
	Hr = MemoryStream->Read(OutBytes.data(), Size, &Read);
	if (FAILED(Hr) || Read != Size)
	{
		OutBytes.clear();
		if (bNeedUninit)
		{
			CoUninitialize();
		}
		return false;
	}

	if (bNeedUninit)
	{
		CoUninitialize();
	}
	return true;
}
#endif // _WIN32

#if defined(MAHO_WITH_LIBKTX)
[[nodiscard]] ETexturePixelFormat MapVkFormatToPixel(ktx_uint32_t VkFormat, bool& bOutSRGB)
{
	bOutSRGB = false;
	switch (VkFormat)
	{
	case 37: // VK_FORMAT_R8G8B8A8_UNORM
		return ETexturePixelFormat::RGBA8;
	case 43: // VK_FORMAT_R8G8B8A8_SRGB
		bOutSRGB = true;
		return ETexturePixelFormat::RGBA8;
	case 23: // VK_FORMAT_R8G8B8_UNORM
		return ETexturePixelFormat::RGB8;
	case 29: // VK_FORMAT_R8G8B8_SRGB
		bOutSRGB = true;
		return ETexturePixelFormat::RGB8;
	default:
		return ETexturePixelFormat::BlockCompressed;
	}
}

[[nodiscard]] bool DecodeKtx2(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	FDecodedImage& Out)
{
	ktxTexture* Texture = nullptr;
	const KTX_error_code Result = ktxTexture_CreateFromMemory(
		Bytes,
		ByteCount,
		KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
		&Texture);
	if (Result != KTX_SUCCESS || !Texture)
	{
		MAHO_CORE_ERROR("TextureImageCodec: ktxTexture_CreateFromMemory failed ({})", static_cast<int>(Result));
		return false;
	}

	if (Texture->classId != ktxTexture2_c)
	{
		MAHO_CORE_WARN("TextureImageCodec: non-KTX2 texture; attempting generic load");
	}

	Out.Width = Texture->baseWidth;
	Out.Height = Texture->baseHeight;
	Out.Depth = (std::max)(Texture->baseDepth, 1u);
	Out.MipCount = (std::max)(Texture->numLevels, 1u);
	Out.ArrayLayers = (std::max)(Texture->numLayers, 1u);

	if (Texture->isCubemap)
	{
		Out.Dimension = Texture->numLayers > 1 ? ETextureDimension::CubeArray : ETextureDimension::Cube;
		Out.ArrayLayers = 6u * (std::max)(Texture->numLayers, 1u);
	}
	else if (Texture->numDimensions == 3)
	{
		Out.Dimension = ETextureDimension::Tex3D;
	}
	else if (Texture->numLayers > 1)
	{
		Out.Dimension = ETextureDimension::Tex2DArray;
	}
	else
	{
		Out.Dimension = ETextureDimension::Tex2D;
	}

	bool bSRGB = true;
	ktx_uint32_t VkFormat = 0;
	if (Texture->classId == ktxTexture2_c)
	{
		VkFormat = reinterpret_cast<ktxTexture2*>(Texture)->vkFormat;
	}
	Out.Format = MapVkFormatToPixel(VkFormat, bSRGB);
	Out.bSRGB = bSRGB;

	const ktx_size_t DataSize = ktxTexture_GetDataSize(Texture);
	ktx_uint8_t* Data = ktxTexture_GetData(Texture);
	if (!Data || DataSize == 0)
	{
		ktxTexture_Destroy(Texture);
		MAHO_CORE_ERROR("TextureImageCodec: KTX has no image data");
		return false;
	}

	Out.Pixels.assign(Data, Data + DataSize);
	ktxTexture_Destroy(Texture);
	return true;
}

[[nodiscard]] bool EncodeKtx2(const FTexture& Texture, const std::string& DestinationPath)
{
	if (Texture.GetPixels().empty() || Texture.GetWidth() == 0 || Texture.GetHeight() == 0)
	{
		MAHO_CORE_ERROR("TextureImageCodec: EncodeKtx2 needs CPU pixels");
		return false;
	}

	ktxTexture2* Texture2 = nullptr;
	ktxTextureCreateInfo CreateInfo{};
	CreateInfo.vkFormat = Texture.IsSRGB() ? 43u : 37u; // RGBA8_SRGB / UNORM
	CreateInfo.baseWidth = Texture.GetWidth();
	CreateInfo.baseHeight = Texture.GetHeight();
	CreateInfo.baseDepth = Texture.GetDepth();
	CreateInfo.numDimensions = (Texture.GetDimension() == ETextureDimension::Tex3D) ? 3u : 2u;
	CreateInfo.numLevels = Texture.GetMipCount();
	CreateInfo.numLayers = 1;
	CreateInfo.numFaces = 1;
	CreateInfo.isArray = KTX_FALSE;
	CreateInfo.generateMipmaps = KTX_FALSE;

	switch (Texture.GetDimension())
	{
	case ETextureDimension::Cube:
		CreateInfo.numFaces = 6;
		CreateInfo.numLayers = 1;
		break;
	case ETextureDimension::CubeArray:
		CreateInfo.numFaces = 6;
		CreateInfo.numLayers = (std::max)(1u, Texture.GetArrayLayers() / 6u);
		CreateInfo.isArray = KTX_TRUE;
		break;
	case ETextureDimension::Tex2DArray:
		CreateInfo.numLayers = Texture.GetArrayLayers();
		CreateInfo.isArray = KTX_TRUE;
		break;
	default:
		break;
	}

	KTX_error_code Result = ktxTexture2_Create(&CreateInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &Texture2);
	if (Result != KTX_SUCCESS || !Texture2)
	{
		MAHO_CORE_ERROR("TextureImageCodec: ktxTexture2_Create failed ({})", static_cast<int>(Result));
		return false;
	}

	const bool bMultiSubImage = CreateInfo.numFaces > 1 || CreateInfo.numLayers > 1
		|| CreateInfo.isArray == KTX_TRUE;
	if (!bMultiSubImage)
	{
		Result = ktxTexture_SetImageFromMemory(
			ktxTexture(Texture2),
			0,
			0,
			0,
			Texture.GetPixels().data(),
			Texture.GetPixels().size());
		if (Result != KTX_SUCCESS)
		{
			MAHO_CORE_ERROR("TextureImageCodec: ktxTexture_SetImageFromMemory failed ({})", static_cast<int>(Result));
			ktxTexture_Destroy(ktxTexture(Texture2));
			return false;
		}
	}
	else
	{
		// Contiguous faces/layers: one SetImageFromMemory per subimage.
		const std::uint32_t FaceCount = CreateInfo.numFaces;
		const std::uint32_t LayerCount = (std::max)(1u, CreateInfo.numLayers);
		const std::size_t FaceBytes =
			static_cast<std::size_t>(Texture.GetWidth()) * Texture.GetHeight() * 4u;
		const std::uint8_t* Cursor = Texture.GetPixels().data();
		std::size_t Remaining = Texture.GetPixels().size();
		bool bOk = true;
		for (std::uint32_t Layer = 0; Layer < LayerCount && bOk; ++Layer)
		{
			for (std::uint32_t Face = 0; Face < FaceCount && bOk; ++Face)
			{
				if (Remaining < FaceBytes)
				{
					bOk = false;
					break;
				}
				Result = ktxTexture_SetImageFromMemory(
					ktxTexture(Texture2),
					0,
					Layer,
					Face,
					Cursor,
					FaceBytes);
				bOk = (Result == KTX_SUCCESS);
				Cursor += FaceBytes;
				Remaining -= FaceBytes;
			}
		}
		if (!bOk)
		{
			MAHO_CORE_ERROR("TextureImageCodec: ktxTexture_SetImageFromMemory failed ({})", static_cast<int>(Result));
			ktxTexture_Destroy(ktxTexture(Texture2));
			return false;
		}
	}

	Result = ktxTexture_WriteToNamedFile(ktxTexture(Texture2), DestinationPath.c_str());
	ktxTexture_Destroy(ktxTexture(Texture2));
	if (Result != KTX_SUCCESS)
	{
		MAHO_CORE_ERROR("TextureImageCodec: ktxTexture_WriteToNamedFile failed ({})", static_cast<int>(Result));
		return false;
	}
	return true;
}
#endif // MAHO_WITH_LIBKTX

} // namespace

std::string GetExtensionLower(std::string_view Path)
{
	const std::size_t Dot = Path.find_last_of('.');
	if (Dot == std::string_view::npos || Dot + 1 >= Path.size())
	{
		return {};
	}
	std::string Ext(Path.substr(Dot + 1));
	for (char& Ch : Ext)
	{
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	}
	return Ext;
}

bool IsKtx2Extension(std::string_view Ext)
{
	return Ext == "ktx2" || Ext == "ktx";
}

bool IsRasterExtension(std::string_view Ext)
{
	return Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "tga" || Ext == "bmp"
		|| Ext == "tif" || Ext == "tiff" || Ext == "gif" || Ext == "webp" || Ext == "hdr"
		|| Ext == "exr" || Ext == "dds";
}

bool PathLooksLikeCubeArray(std::string_view Path)
{
	return ContainsInsensitive(Path, ".cubearray.") || ContainsInsensitive(Path, "_cubearray.");
}

bool PathLooksLikeCube(std::string_view Path)
{
	if (PathLooksLikeCubeArray(Path))
	{
		return false;
	}
	return ContainsInsensitive(Path, ".cube.") || ContainsInsensitive(Path, ".cubemap.")
		|| ContainsInsensitive(Path, "_cube.") || ContainsInsensitive(Path, "_cubemap.");
}

bool PathLooksLikeTexture3D(std::string_view Path)
{
	return ContainsInsensitive(Path, ".3d.") || ContainsInsensitive(Path, ".volume.");
}

bool PathLooksLikeTexture2DArray(std::string_view Path)
{
	return ContainsInsensitive(Path, ".2darray.") || ContainsInsensitive(Path, ".array.ktx");
}

bool DecodeFromMemory(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FDecodedImage& Out)
{
	if (!Bytes || ByteCount == 0)
	{
		return false;
	}

	const std::string Ext = GetExtensionLower(SourcePathHint);
	if (IsKtx2Extension(Ext))
	{
#if defined(MAHO_WITH_LIBKTX)
		return DecodeKtx2(Bytes, ByteCount, Out);
#else
		MAHO_CORE_ERROR("TextureImageCodec: KTX2 requested but MAHO_WITH_LIBKTX is off");
		return false;
#endif
	}

#if defined(_WIN32)
	if (!DecodeRasterWic(Bytes, ByteCount, Out))
	{
		return false;
	}
#else
	MAHO_CORE_ERROR("TextureImageCodec: raster decode requires Win32 WIC or OpenImageIO");
	return false;
#endif

	if (PathLooksLikeCubeArray(SourcePathHint))
	{
		Out.Dimension = ETextureDimension::CubeArray;
	}
	else if (PathLooksLikeCube(SourcePathHint))
	{
		Out.Dimension = ETextureDimension::Cube;
		Out.ArrayLayers = 6;
	}
	else if (PathLooksLikeTexture3D(SourcePathHint))
	{
		Out.Dimension = ETextureDimension::Tex3D;
	}
	else if (PathLooksLikeTexture2DArray(SourcePathHint))
	{
		Out.Dimension = ETextureDimension::Tex2DArray;
	}
	return true;
}

bool EncodeToFile(const FTexture& Texture, const std::string& DestinationPath, bool bOverwrite)
{
	namespace fs = std::filesystem;
	std::error_code ErrorCode;
	const fs::path Dest = PathFromUtf8(DestinationPath);
	if (!bOverwrite && fs::exists(Dest, ErrorCode) && !ErrorCode)
	{
		MAHO_CORE_ERROR("TextureImageCodec: destination exists '{}'", DestinationPath);
		return false;
	}
	if (Dest.has_parent_path())
	{
		fs::create_directories(Dest.parent_path(), ErrorCode);
	}

	const std::string Ext = GetExtensionLower(DestinationPath);
	if (IsKtx2Extension(Ext))
	{
#if defined(MAHO_WITH_LIBKTX)
		return EncodeKtx2(Texture, DestinationPath);
#else
		MAHO_CORE_ERROR("TextureImageCodec: KTX2 export requires MAHO_WITH_LIBKTX");
		return false;
#endif
	}

#if defined(_WIN32)
	return EncodeRasterWic(Texture, DestinationPath);
#else
	(void)Texture;
	MAHO_CORE_ERROR("TextureImageCodec: raster encode requires Win32 WIC or OpenImageIO");
	return false;
#endif
}

bool EncodePngToMemory(const FTexture& Texture, std::vector<std::uint8_t>& OutBytes)
{
	OutBytes.clear();
#if defined(_WIN32)
	if (!EncodeRasterWicToMemory(Texture, GUID_ContainerFormatPng, OutBytes))
	{
		MAHO_CORE_ERROR("TextureImageCodec: EncodePngToMemory failed");
		return false;
	}
	return true;
#else
	(void)Texture;
	MAHO_CORE_ERROR("TextureImageCodec: EncodePngToMemory requires Win32 WIC");
	return false;
#endif
}

bool ApplyDecodedToTexture(FTexture& Texture, FDecodedImage&& Image)
{
	Texture.SetCpuImage(
		Image.Dimension,
		Image.Format,
		Image.Width,
		Image.Height,
		Image.Depth,
		Image.ArrayLayers,
		Image.MipCount,
		Image.bSRGB,
		std::move(Image.Pixels));
	return Texture.GetWidth() > 0 && Texture.GetHeight() > 0 && !Texture.GetPixels().empty();
}

} // namespace TextureImageCodec
} // namespace Maho
