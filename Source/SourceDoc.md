<!-- mahogen -->
# Source

## 子层级

- [Private](Private/PrivateDoc.md)
- [Public](Public/PublicDoc.md)
<!-- mahogen end -->

## 概念——源码根

引擎源码根：`Public/` 放接口与头文件，`Private/` 放实现（.cpp）。

- [Public/PublicDoc.md](Public/PublicDoc.md) — 接口 + 聚合头
- [Private/PrivateDoc.md](Private/PrivateDoc.md) — 实现

引擎是**纯脚手架**——只提供类型无关基建（Core）+ 层体系（Engine/Layer.h）。具体服务全部是可安装插件（`Plugins/Common/`）。引擎 .cpp 仅 `Assembly.cpp` / `Fatal.cpp`，其余基建 header-only（模板 + inline）。

### 分层

- **Core**（`Public/Core/`）：类型无关基建积木——TypeList/Queue/Topology/Query/Singleton/Interface/Extension/Assembly/Fatal/Schedulers/ThreadPool/ThreadedServer。
- **Engine**（`Public/Engine/`）：层体系——`Layer.h`（FLayerBase/FLayer/命令/DispatchInstance/插件宏）。
- **Plugins**（`Plugins/`）：可安装插件——`Common/`（14 服务插件）+ `Gameplay/`（Layer 插件）。引擎零 app 假设，一切逻辑在插件。
