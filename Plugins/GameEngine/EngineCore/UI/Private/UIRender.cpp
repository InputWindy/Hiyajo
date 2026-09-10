#include <UIRender.h>

namespace Maho { namespace UI {

EUIInputFlags operator|(EUIInputFlags A, EUIInputFlags B)
{
	return static_cast<EUIInputFlags>(static_cast<std::uint32_t>(A) | static_cast<std::uint32_t>(B));
}

EUIInputFlags operator&(EUIInputFlags A, EUIInputFlags B)
{
	return static_cast<EUIInputFlags>(static_cast<std::uint32_t>(A) & static_cast<std::uint32_t>(B));
}

bool HasFlag(EUIInputFlags V, EUIInputFlags F)
{
	return (static_cast<std::uint32_t>(V) & static_cast<std::uint32_t>(F)) != 0u;
}

}} // namespace Maho::UI
