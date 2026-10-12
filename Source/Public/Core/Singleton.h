#pragma once

namespace Maho
{

template <typename T>
class TSingleton
{
protected:
	TSingleton() = default;
	~TSingleton() = default;

	TSingleton(const TSingleton&) = delete;
	TSingleton& operator=(const TSingleton&) = delete;
};

}

