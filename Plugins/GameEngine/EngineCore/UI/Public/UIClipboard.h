#pragma once

#include "UIApi.h"

#include <Core/Delegate.h>

#include <functional>
#include <string>
#include <string_view>

namespace Maho { namespace UI {

/** 宿主注入的剪贴板读写（平台层：Win32 / SDL / …）。
 *  UI 插件不依赖 `Platform`，文本剪贴板能力由拥有窗口的宿主注入
 *  （先例：`FResourceSystem::SetReadback` 的注入风格）。 */
using FUIClipboardGetHandler = std::function<std::string()>;
using FUIClipboardSetHandler = std::function<void(std::string_view)>;

/** 注册剪贴板处理器，返回撤销凭据。与 `BindUIResourceResolver` 同一份契约：
 *  **token 必须由注册者在自己的 teardown 里交还**（`UnbindUIClipboardHandlers`），
 *  否则 `FUIViewRegistry::IShutdown` 会清掉槽并记名报错 —— 不崩，但那次注入的能力会丢。 */
MAHO_UI_API FSubscriptionID BindUIClipboardHandlers(std::string_view Owner,
	FUIClipboardGetHandler Get, FUIClipboardSetHandler Set);

/** 交还一个注册凭据。token 不匹配（已被覆盖 / 已交还）时是 no-op。 */
MAHO_UI_API void UnbindUIClipboardHandlers(FSubscriptionID Token);

/** 读剪贴板文本：未注入时返回空串，不抛不崩。 */
MAHO_UI_API std::string GetUIClipboardText();

/** 写剪贴板文本：未注入时忽略。 */
MAHO_UI_API void SetUIClipboardText(std::string_view Text);

}} // namespace Maho::UI
