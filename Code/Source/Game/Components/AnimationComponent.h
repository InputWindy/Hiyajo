#pragma once

#include "Game/Components/ComponentCommon.h"

#include <cstring>
#include <type_traits>

namespace Maho
{

struct FAnimationComponent
{
	char AnimationClipPath[ECSComponentAssetPathMax] = {};
	float Time = 0.0f;
	float Speed = 1.0f;
	bool bLoop = true;
	bool bPlaying = true;

	void Play(const char* Clip, float InSpeed = 1.0f, bool InLoop = true)
	{
		if (Clip)
		{
			std::strncpy(AnimationClipPath, Clip, ECSComponentAssetPathMax - 1);
			AnimationClipPath[ECSComponentAssetPathMax - 1] = '\0';
		}
		Time = 0.0f;
		Speed = InSpeed;
		bLoop = InLoop;
		bPlaying = true;
	}

	void Stop()
	{
		bPlaying = false;
	}

	void Pause()
	{
		bPlaying = false;
	}
};

} // namespace Maho

static_assert(std::is_trivially_copyable_v<Maho::FAnimationComponent>, "FAnimationComponent must be trivially copyable");
