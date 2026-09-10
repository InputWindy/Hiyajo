#pragma once

#include "UIApi.h"

#include <functional>
#include <string>
#include <string_view>

namespace Maho { namespace UI {

/** 宿主注入的剪贴板读写（平台层：Win32 / SDL / …）。
 *  UI 插件不依赖 `Platform`，文本剪贴板能力由拥有窗口的宿主注入
 *  （先例：`FResourceSystem::SetReadback` 的注入风格）。 */
using FUIClipboardGetHandler = std::function<std::string()>;
using FUIClipboardSetHandler = std::function<void(std::string_view)>;

/** 注入 / 覆盖处理器（传空清空）。 */
MAHO_UI_API void SetUIClipboardHandlers(FUIClipboardGetHandler Get, FUIClipboardSetHandler Set);

/** 读剪贴板文本：未注入时返回空串，不抛不崩。 */
MAHO_UI_API std::string GetUIClipboardText();

/** 写剪贴板文本：未注入时忽略。 */
MAHO_UI_API void SetUIClipboardText(std::string_view Text);

}} // namespace Maho::UI
