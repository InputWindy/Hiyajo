#pragma once

#include "Game/Components/ComponentCommon.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Maho
{

constexpr std::size_t ECSMaxPropertyOverrides = 16;
constexpr std::size_t ECSMaxTextureOverrides = 8;

struct FPropertyOverride
{
	char Name[ECSComponentNameMax] = {};
	float Data[4] = {};
	std::uint32_t DataSize = 0; // 1–4
};

struct FTextureOverride
{
	char Name[ECSComponentNameMax] = {};
	char Path[ECSComponentAssetPathMax] = {};
};

struct FMaterialComponent
{
	char ShaderPath[ECSComponentAssetPathMax] = {};

	FPropertyOverride Overrides[ECSMaxPropertyOverrides] = {};
	std::uint32_t OverrideCount = 0;

	FTextureOverride TextureOverrides[ECSMaxTextureOverrides] = {};
	std::uint32_t TextureOverrideCount = 0;

	[[nodiscard]] bool IsValid() const
	{
		return ShaderPath[0] != '\0';
	}

	void SetFloat(const char* Name, float V)
	{
		std::uint32_t Idx = FindOrAddOverride(Name);
		if (Idx < ECSMaxPropertyOverrides)
		{
			Overrides[Idx].Data[0] = V;
			Overrides[Idx].DataSize = 1;
		}
	}

	void SetColor(const char* Name, float R, float G, float B, float A)
	{
		std::uint32_t Idx = FindOrAddOverride(Name);
		if (Idx < ECSMaxPropertyOverrides)
		{
			Overrides[Idx].Data[0] = R;
			Overrides[Idx].Data[1] = G;
			Overrides[Idx].Data[2] = B;
			Overrides[Idx].Data[3] = A;
			Overrides[Idx].DataSize = 4;
		}
	}

	void SetTexture(const char* Name, const char* TexPath)
	{
		SetFloat(Name, 0.0f);
		SetOverridePath(Name, TexPath);
	}

	void SetOverridePath(const char* Name, const char* Path)
	{
		if (!Name || !Path)
		{
			return;
		}
		std::uint32_t Idx = FindOrAddTexOverride(Name);
		if (Idx < ECSMaxTextureOverrides)
		{
			std::strncpy(TextureOverrides[Idx].Path, Path, ECSComponentAssetPathMax - 1);
			TextureOverrides[Idx].Path[ECSComponentAssetPathMax - 1] = '\0';
		}
	}

	[[nodiscard]] const char* GetOverridePath(const char* Name) const
	{
		if (!Name)
		{
			return nullptr;
		}
		for (std::uint32_t I = 0; I < TextureOverrideCount; ++I)
		{
			if (std::strcmp(TextureOverrides[I].Name, Name) == 0)
			{
				return TextureOverrides[I].Path;
			}
		}
		return nullptr;
	}

	[[nodiscard]] const FPropertyOverride* FindOverride(const char* Name) const
	{
		if (!Name)
		{
			return nullptr;
		}
		for (std::uint32_t I = 0; I < OverrideCount; ++I)
		{
			if (std::strcmp(Overrides[I].Name, Name) == 0)
			{
				return &Overrides[I];
			}
		}
		return nullptr;
	}

private:
	std::uint32_t FindOrAddOverride(const char* Name)
	{
		for (std::uint32_t I = 0; I < OverrideCount; ++I)
		{
			if (std::strcmp(Overrides[I].Name, Name) == 0)
			{
				return I;
			}
		}
		if (OverrideCount >= ECSMaxPropertyOverrides)
		{
			return ECSMaxPropertyOverrides;
		}
		std::uint32_t Idx = OverrideCount++;
		std::strncpy(Overrides[Idx].Name, Name, ECSComponentNameMax - 1);
		Overrides[Idx].Name[ECSComponentNameMax - 1] = '\0';
		return Idx;
	}

	std::uint32_t FindOrAddTexOverride(const char* Name)
	{
		for (std::uint32_t I = 0; I < TextureOverrideCount; ++I)
		{
			if (std::strcmp(TextureOverrides[I].Name, Name) == 0)
			{
				return I;
			}
		}
		if (TextureOverrideCount >= ECSMaxTextureOverrides)
		{
			return ECSMaxTextureOverrides;
		}
		std::uint32_t Idx = TextureOverrideCount++;
		std::strncpy(TextureOverrides[Idx].Name, Name, ECSComponentNameMax - 1);
		TextureOverrides[Idx].Name[ECSComponentNameMax - 1] = '\0';
		return Idx;
	}
};

} // namespace Maho

static_assert(std::is_trivially_copyable_v<Maho::FMaterialComponent>, "FMaterialComponent must be trivially copyable");
