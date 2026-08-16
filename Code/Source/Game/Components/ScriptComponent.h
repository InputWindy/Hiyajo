#pragma once

#include "Game/Components/ComponentCommon.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Maho
{

constexpr std::size_t ECSMaxScriptParams = 16;

struct FScriptParam
{
	char Key[ECSComponentNameMax] = {};
	char Value[ECSComponentAssetPathMax] = {};
};

struct FScriptComponent
{
	char ScriptPath[ECSComponentAssetPathMax] = {};
	bool bEnabled = true;

	FScriptParam Params[ECSMaxScriptParams] = {};
	std::uint32_t ParamCount = 0;

	void SetParam(const char* Key, const char* Value)
	{
		if (!Key || !Value)
		{
			return;
		}
		for (std::uint32_t I = 0; I < ParamCount; ++I)
		{
			if (std::strcmp(Params[I].Key, Key) == 0)
			{
				std::strncpy(Params[I].Value, Value, ECSComponentAssetPathMax - 1);
				Params[I].Value[ECSComponentAssetPathMax - 1] = '\0';
				return;
			}
		}
		if (ParamCount >= ECSMaxScriptParams)
		{
			return;
		}
		std::uint32_t Idx = ParamCount++;
		std::strncpy(Params[Idx].Key, Key, ECSComponentNameMax - 1);
		Params[Idx].Key[ECSComponentNameMax - 1] = '\0';
		std::strncpy(Params[Idx].Value, Value, ECSComponentAssetPathMax - 1);
		Params[Idx].Value[ECSComponentAssetPathMax - 1] = '\0';
	}

	[[nodiscard]] const char* GetParam(const char* Key) const
	{
		if (!Key)
		{
			return nullptr;
		}
		for (std::uint32_t I = 0; I < ParamCount; ++I)
		{
			if (std::strcmp(Params[I].Key, Key) == 0)
			{
				return Params[I].Value;
			}
		}
		return nullptr;
	}

	[[nodiscard]] bool IsValid() const
	{
		return ScriptPath[0] != '\0';
	}
};

} // namespace Maho

static_assert(std::is_trivially_copyable_v<Maho::FScriptComponent>, "FScriptComponent must be trivially copyable");
