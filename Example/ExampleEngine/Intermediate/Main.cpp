// ═══════════════════════════════════════════════════════════════════════
//  Maho 项目入口（code-gen，无需改动）
//
//  Source/ 文件夹里应该只有本文件。一切项目逻辑都是插件：
//    - 项目默认插件：Extension/<ProjectName>/
//    - 手动创建的插件：Extension/<其他插件名>/
//
//  入口只负责：安装（加载）默认插件 DLL → CreateLayer → Main 执行。
// ═══════════════════════════════════════════════════════════════════════
#if defined(_WIN32)
#	include <EntryPointWindows.h>
#elif defined(__ANDROID__)
#	include <EntryPointAndroid.h>
#elif defined(__APPLE__)
#	include <EntryPointIOS.h>
#elif defined(__linux__)
#	include <EntryPointLinux.h>
#endif
