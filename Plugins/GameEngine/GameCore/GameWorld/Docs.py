# GameWorld 插件的内容脚本（plugin_docs.py 会 exec 本文件，docs_builder 已注入为 D）。
#
# 内容与磁盘上的 Public/*.h 逐字对应：先声明头，再声明宏 / 自由函数 / 类 / 结构 / 枚举，
# 每个类按 public → private 的**源码顺序**声明，访问域用 SetAccess 如实标注。

# ══════════════════════════════════════════════════════════════════════════════
# Public/GameWorldApi.h —— 导出宏
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/GameWorldApi.h", Title="GameWorldApi.h —— 导出宏",
         Desc="这个插件的导出/导入开关：`MAHO_GAMEWORLD_API` 按 `MAHO_GAMEWORLD_MODULE_EXPORTS` "
              "在 `MAHO_EXPORT` / `MAHO_IMPORT` 之间切换，后者由 codegen 只对本插件 DLL 定义。\n"
              "**为什么必须有它**：`FGameWorld` 的实例由宿主（另一个模块）构造、卸载时又由宿主析构。"
              "若类不带导出标记，vftable 与删除析构会作为 COMDAT **每个模块各生成一份**，"
              "析构时就会在已卸载的映像里跑代码 —— 那是静默的 `0xC0000005`，不是编译错误。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("Core/Export.h", "`MAHO_EXPORT` / `MAHO_IMPORT` 两个底层标记的定义处")

D.Macro("MAHO_GAMEWORLD_API", "MAHO_EXPORT / MAHO_IMPORT（按 MAHO_GAMEWORLD_MODULE_EXPORTS 切换）",
        Desc="本插件所有跨 DLL 的类型都挂这个标记：`FGameWorld`、`GetGameWorld()`、6 个 ECS stage "
             "接口、以及 `CreateFrame()` 导出函数。若将来新增跨 DLL 的类型而不带它，"
             "就会重新引入「按值返回 / 容器内构造」处的 vptr 悬垂。")

# ══════════════════════════════════════════════════════════════════════════════
# Public/Entity.h —— 实体句柄 + 身份注册表
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/Entity.h", Title="Entity.h —— 实体句柄 + 身份注册表",
         Desc="ECS 的对象模型最小一半：**`FEntity`** 是 `(Index, Generation)` 句柄，"
              "**`FEntityRegistry`** 是 index-aligned 的 Header 数组 + 空闲链表。\n"
              "**为什么用 Generation**：槽位被销毁后会被复用，旧句柄若不携带代，"
              "就会「活过来」指向新实体；代号在销毁时 `++`，旧句柄永远落不上（`IsAlive` 判代）。\n"
              "**为什么槽位只标记不压缩**：实体的 Index 必须在整个生命周期内**稳定**，"
              "因为每个组件池都按同一个 Index 寻址（见 `ComponentPool.h`）。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("cstdint", "`std::uint32_t`：Index / Generation / 空闲链表的元素类型")
D.Row("vector", "`Headers`（index-aligned 槽位头）与 `FreeList`（可复用槽位下标）两条数组")

D.Struct("FEntity", Desc="实体句柄 —— 值语义的 `(Index, Generation)` 对；默认构造即**无效句柄**"
        "（`Index == 0xFFFFFFFF`），所以「零初始化的 FEntity」天然可以安全判空。")
D.SetAccess("public")
D.Field("std::uint32_t Index = 0xFFFFFFFFu", "槽位下标；`0xFFFFFFFF` = 无效（默认值即无效）")
D.Field("std::uint32_t Generation = 0u", "槽位的代号；销毁时自增，用旧代号的句柄作废")
D.Interface("[[nodiscard]] bool IsValid() const", "只看 Index：句柄形状是否有效（不代表还活着，"
            "活性要问 `FEntityRegistry::IsAlive`）")
D.Interface("bool operator==(const FEntity&) const = default",
            "默认三路比较：Index 与 Generation 全等才算同一实体（存在与代号一起比较）")
D.Interface("bool operator!=(const FEntity&) const = default", "默认生成的 `!=`（与 `==` 同源）")

D.Class("FEntityRegistry", Desc="实体身份注册表：每条槽位一个 `FEntityHeader{Generation, bAlive}`，"
        "外加一个空闲下标链表。`Create` 优先复用空闲槽（复用时沿用该槽的当前代号），"
        "`Destroy` 校验代号后才置死 + 自增代号。**不压缩数组** ⇒ 下标永不搬家，"
        "组件池可以用同一个下标对齐存值。")
D.SetAccess("public")
D.Interface("FEntity Create()", "分配一个实体槽：有空闲槽就复用（返回它当前的代号），"
            "否则在尾部新开一个（代号 0）。**不触碰任何组件池** —— 挂组件是调用方的事")
D.Interface("bool Destroy(FEntity E)", "销毁实体：索引越界 / 已死 / 代号不匹配一律返回 false"
            "（陈旧句柄销毁失败是**设计**，不是错误）；成功则置死、代号自增、下标入空闲链表")
D.Interface("[[nodiscard]] bool IsAlive(FEntity E) const", "活性判据：索引在范围内 && 槽位存活 && "
            "代号与句柄一致。所有跨帧持有 `FEntity` 的地方都该用它复核")
D.Interface("[[nodiscard]] std::uint32_t GetCapacity() const", "当前槽位总数（高水位，不因销毁下降）")
D.SetAccess("private")
D.Nested("FEntityHeader", Kind="struct", Desc="一条槽位的头：`Generation`（当前代号）+ `bAlive`（存活位）。"
         "代号与存活位成对存，`Destroy` 需要同时读它们才能拒绝陈旧句柄。")
D.Field("std::vector<FEntityHeader> Headers", "index-aligned 槽位头（下标 = 实体的 Index）")
D.Field("std::vector<std::uint32_t> FreeList", "已销毁的槽位下标，供 `Create` 复用（LIFO，无顺序要求）")

# ══════════════════════════════════════════════════════════════════════════════
# Public/ComponentPool.h —— 组件池（SoA + 类型擦除）
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/ComponentPool.h", Title="ComponentPool.h —— 组件池（SoA + 类型擦除）",
         Desc="组件存储分两层：**`IComponentPool`** 是类型擦除基类（世界用它持有一组异构池），"
              "**`TComponentPool<T>`** 是每种组件类型一个的 SoA 列。\n"
              "**为什么需要类型擦除**：`FGameWorld` 要在一个 `vector<unique_ptr<...>>` 里容纳"
              "「任意组件类型」的池，同时又不能是类模板（它已经是 frame，有静态身份）—— "
              "所以按 `dynamic_cast` 在运行期找回类型。\n"
              "**为什么只标记不压缩**：一个槽位死掉时其它池的同一下标仍在用；压缩会移动元素、"
              "让所有已发出的 `T*` 悬垂。稀疏存放 + 存在位掩码 = 与 `FEntityRegistry` 的 Index 对齐。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("cstdint", "`std::uint32_t Index`：组件按实体 Index 寻址")
D.Row("utility", "`std::move`：`Add` 把值搬进槽位，避免一次拷贝")
D.Row("vector", "`TComponentPool<T>` 的两条 index-aligned 列（值 + 存在位）")

D.Class("IComponentPool", Desc="类型擦除的池基类：只暴露「按 Index 裸访问 / 标记删除 / 容量」三件事，"
        "让世界能统一遍历所有池（例如 `DestroyEntity` 里逐池清槽）。带虚析构 ⇒ 通过 "
        "`unique_ptr<IComponentPool>` 删派生池是安全的。")
D.SetAccess("public")
D.Interface("virtual ~IComponentPool() = default", "虚析构：世界以基类指针持有派生池，删除必须走 vtable")
D.Interface("virtual void* GetRaw(std::uint32_t Index) = 0", "该 Index 上组件的裸指针（不存在即 nullptr）—— "
            "类型擦除层唯一能给的东西")
D.Interface("virtual void Remove(std::uint32_t Index) = 0", "标记该 Index 死掉（清存在位，**不释放存储**）")
D.Interface("[[nodiscard]] virtual std::uint32_t Capacity() const = 0", "当前槽位容量（高水位，遍历上界）")

D.Class("TComponentPool<T>", Base="IComponentPool", Desc="一种组件类型的 SoA 存储：`Items`（值列）+ "
        "`Has`（存在位）。`Add` 按需扩容到 `Index + 1` 并 `MoveAssign` 进槽位；"
        "槽位一经分配就不再移动，因此组件的生命周期与实体的 Index 严格对齐，"
        "池增长时已取得的 `T*` 不会失效（`vector<T>` 的扩容只发生在越界那一刻）。")
D.SetAccess("public")
D.Interface("[[nodiscard]] T* Get(std::uint32_t Index)", "取组件指针；越界或存在位为 false 时 nullptr")
D.Interface("[[nodiscard]] const T* Get(std::uint32_t Index) const", "const 重载（只读遍历用）")
D.Interface("T* Add(std::uint32_t Index, T Value)", "写入（或覆盖）该 Index 的组件，返回池内地址。"
            "参数按值 + `std::move` ⇒ 调用方给右值时零额外拷贝；扩容只在本行发生")
D.Interface("void* GetRaw(std::uint32_t Index) override", "擦除重载：直接转发到 `Get`")
D.Interface("void Remove(std::uint32_t Index) override", "清存在位（越界则忽略）；值留在池里等下次 `Add` 覆盖")
D.Interface("[[nodiscard]] std::uint32_t Capacity() const override", "`Items.size()`：给 `GetAllWithComponent` 当遍历上界")
D.SetAccess("private")
D.Field("std::vector<T> Items", "index-aligned 组件值列（稀疏：槽位可能无值）")
D.Field("std::vector<bool> Has", "index-aligned 存在位掩码，与 `Items` 同长；`Get` 先查它再取地址")

# ══════════════════════════════════════════════════════════════════════════════
# Public/GameWorld.h —— 世界帧 + ECS 子 stage 接口
# ══════════════════════════════════════════════════════════════════════════════

D.Header("Public/GameWorld.h", Title="GameWorld.h —— 世界帧 + ECS 子 stage 接口",
         Desc="`FGameWorld` 是**二房东**：它自己是被宿主挂载的引擎层（10 个引擎 stage），"
              "同时又是一个**子收集器**（`FFrameBuilder<FGameWorld>`），把世界系统（`FUISystem` 等）"
              "当作 peer frame 装进自己的子图，用**自己的一串子 stage 序列**驱动它们。\n"
              "**为什么是二房东而不是一个大类**：世界系统要能独立装卸（热重载 / 关闭时逐个 teardown），"
              "这只有「它们各自是 frame、由收集器管生命周期」才做得到；世界只提供 stage 接口"
              "（`IProcessInput` 等）与 ECS 对象模型。\n"
              "**自己的一帧在 `Tick` 里**：宿主只驱动 10 个引擎 stage，ECS 的 per-stage 图"
              "（input → fixed ×N → update/late）由世界在 `Tick` 内三次 `Execute<序列>()` 展开。")

D.Card("包含的头文件")
D.Table("头文件", "功能")
D.Row("GameWorldApi.h", "`MAHO_GAMEWORLD_API`：本头里每个类都要跨 DLL")
D.Row("Entity.h", "`FEntity` / `FEntityRegistry` —— 世界的对象模型一半")
D.Row("ComponentPool.h", "`IComponentPool` / `TComponentPool<T>` —— 另一半")
D.Row("Maho.h", "引擎总头（`Core/Export.h` 等基础设施的统一入口）")
D.Row("Engine/Frame.h", "`MAHO_DECLARE_FRAME` / `MAHO_DECLARE_STAGE_DISPATCH` —— 身份与 stage 分派")
D.Row("Engine/FrameBuilder.h", "`FFrameBuilder<FGameWorld>`：子收集器（装载/卸载世界系统 + 子图）")
D.Row("Engine/Engine.h", "`FEngineBase` 与 10 个引擎 stage 接口 `IPreInit`…`IPostShutdown`")
D.Row("Core/TypeList.h", "`TTypeList`：三条子 stage 序列的类型")
D.Row("chrono", "`steady_clock`：帧间隔与固定步进累加器的时间源")
D.Row("memory / utility / vector", "`unique_ptr` 持有异构池；`move` 搬值；`vector` 是池集合与查询结果的容器")

D.Card("宏与内部类型别名")
D.Table("签名", "说明")
D.Row("MAHO_DECLARE_FRAME(FGameWorld)",
      "生成 `StaticName()`（`#FGameWorld` 字面量 ⇒ 静态存储 ⇒ 可安全当 `FTaskKey::Name`）/ "
      "`GetName()` / `CreateFrame()`（DLL 工厂，宿主按符号名查找）/ `GetModulePath()`")
D.Row("MAHO_DECLARE_STAGE_DISPATCH(世界, 子 stage, Cast, Method)",
      "把「子 stage → 世界系统的方法」铺成 `Invoke<Stage, FGameWorld>` 全特化；"
      "**必须写在 `Maho` 命名空间作用域**（不是 `Maho::GameWorld` 里），否则全特化不到"
      "`Engine/Frame.h` 里的主模板。本头为 6 个 ECS 子 stage 各写一条")
D.Row("FInputStages = TTypeList<IProcessInput>", "输入组：一帧一次，先于任何 fixed/update")
D.Row("FFixedStages = TTypeList<IFixedUpdate>", "固定步进组：一帧 0..N 次，固定 dt")
D.Row("FPostStages = TTypeList<IUpdate, ILateUpdate>", "帧末组：update 之后 late update（同一序列 ⇒ 自带先后边）")

D.Card("ECS 子 stage 顺序（Unity 顺序）")
D.Table("子 stage 接口", "方法", "时序位置")
D.Row("IOnInstalled", "OnInstalled(FGameWorld&)", "世界系统**装入安全点**：一次性，先于本帧任何 stage")
D.Row("IProcessInput", "ProcessInput(FGameWorld&)", "每帧一次、最早 —— 模拟必须读到一个稳定的输入状态")
D.Row("IFixedUpdate", "FixedUpdate(FGameWorld&)", "固定步进：0..N 次/帧，每次用同一个固定 dt")
D.Row("IUpdate", "Update(FGameWorld&)", "每帧一次，在所有 fixed 步之后（帧内逻辑）")
D.Row("ILateUpdate", "LateUpdate(FGameWorld&)", "每帧一次，在 update 之后（跟随/相机/清理）")
D.Row("IPreUnInstall", "PreUnInstall(FGameWorld&)", "世界系统**卸出安全点**：先于实例销毁（撤销跨模块订阅）")

D.Card("自由函数")
D.Table("签名", "说明")
D.Row("MAHO_GAMEWORLD_API FGameWorld* GetGameWorld()",
      "全局世界访问器 —— 跨 DLL 走**函数**而不是共享变量（每个模块一份静态变量的老问题）。"
      "值为 nullptr 直到世界 `Initialize`；`Shutdown` 里清回 nullptr，所以系统不该把它缓存过卸载点")

D.Struct("FTransform", Desc="示例组件：三个 float 的位置。放在这里只为演示对象模型"
        "（`AddComponent<FTransform>` → `GetComponent<FTransform>` 的往返），"
        "按设计**组件是普通聚合类型**：无基类、无虚函数、可平凡搬移 —— 池按值存储并 `move`，"
        "所以组件不该有自定义析构，否则跨模块析构会在池被清空时集中发生（见 `Shutdown`）。")
D.SetAccess("public")
D.Field("float X = 0.f", "位置 X")
D.Field("float Y = 0.f", "位置 Y")
D.Field("float Z = 0.f", "位置 Z")

D.Class("IOnInstalled", Desc="**装入安全点**的子 stage：世界系统装进世界后跑一次。"
        "它独立于帧循环（属于收集器的 install 批，不属于任何一帧），"
        "所以「读世界 / 建自己的资源」放这里，而不是放第一帧的 `Update`。")
D.SetAccess("public")
D.Interface("virtual ~IOnInstalled() = default", "虚析构：跨 DLL 持有，删除必须走 vtable")
D.Interface("virtual void OnInstalled(FGameWorld&) = 0", "装好后收到世界引用（一次）")

D.Class("IProcessInput", Desc="每帧**最早**的子 stage：所有系统在这里把输入收敛成一份状态。"
        "世界对它 `Execute` 后**立刻 `Wait`**（不等下一帧），因为后续的模拟步必须读到稳定的输入 —— "
        "输入晚一帧与输入半更新都会让模拟不可复现。")
D.SetAccess("public")
D.Interface("virtual ~IProcessInput() = default", "虚析构（同 `IOnInstalled`）")
D.Interface("virtual void ProcessInput(FGameWorld&) = 0", "每帧一次，先于 fixed / update")

D.Class("IFixedUpdate", Desc="**固定步进**子 stage：用固定 dt 跑 0..N 次/帧，与渲染帧率解耦"
        "（物理 / 确定性模拟要求步长恒定）。世界**每一步单独 `Execute` + `Wait`**："
        "两步之间必须有先后关系，否则同一实体的两次积分会交错。")
D.SetAccess("public")
D.Interface("virtual ~IFixedUpdate() = default", "虚析构（同 `IOnInstalled`）")
D.Interface("virtual void FixedUpdate(FGameWorld&) = 0", "一步固定 dt 的模拟")

D.Class("IUpdate", Desc="每帧一次的主更新：所有 fixed 步跑完之后（累加器已被扣减完），"
        "所以它看到的是本帧模拟的**最终**状态。与 `ILateUpdate` 同在 `FPostStages` 序列里，"
        "序列内的先后由批的帧内链给出，无需额外声明边。")
D.SetAccess("public")
D.Interface("virtual ~IUpdate() = default", "虚析构（同 `IOnInstalled`）")
D.Interface("virtual void Update(FGameWorld&) = 0", "帧内逻辑（在 fixed 之后）")

D.Class("ILateUpdate", Desc="`FPostStages` 序列里的第二个 stage：跟随 / 相机 / 需要在主逻辑之后读最终"
        "变换的收尾逻辑。它与 `IUpdate` 同序列 ⇒ 天然 order 在 update 之后；"
        "该序列**不追加尾部 `Wait`**，所以它与下一帧的帧首 `Wait` 重叠运行（跨帧流水）。")
D.SetAccess("public")
D.Interface("virtual ~ILateUpdate() = default", "虚析构（同 `IOnInstalled`）")
D.Interface("virtual void LateUpdate(FGameWorld&) = 0", "帧末收尾（在 update 之后）")

D.Class("IPreUnInstall", Desc="**卸出安全点**的子 stage：实例销毁**之前**跑。"
        "为什么必须有：世界系统常把 `std::function` / 回调绑进别的 DLL 的事件"
        "（例如把视图注册进 UI 注册表），裸的成员析构会跳过这一步，留下跨模块的悬垂订阅。")
D.SetAccess("public")
D.Interface("virtual ~IPreUnInstall() = default", "虚析构（同 `IOnInstalled`）")
D.Interface("virtual void PreUnInstall(FGameWorld&) = 0", "撤掉订阅 / 注销视图，然后实例才会被释放")

D.Class("FGameWorld", Base="FFrameExtension + IPipeline<10 个引擎 stage> + FFrameBuilder<FGameWorld>",
        Desc="世界帧：两个基类（`FFrameExtension` 声明层 + `IPipeline<...>` 有序 stage 列表），"
             "外加**第三个** —— `FFrameBuilder<FGameWorld>` 使它同时是子收集器，"
             "拥有自己的图、自己的线程池、自己的世界系统实例。\n"
             "它**全部 10 个引擎 stage 都实现**（接口是纯虚的，一个都不能漏），但只有 "
             "`Initialize` / `Tick` / `Shutdown` 有内容：世界只负责托管与调度，"
             "ECS 的一帧在 `Tick` 里展开。\n"
             "**构造里只声明一条反向边**：`IShutdown` 阻塞 `FUIViewRegistry`（按**名字**寻址），"
             "让 UI 注册表的 shutdown 晚于世界的 shutdown —— 因为世界系统在自己的 `PreUnInstall` 里"
             "注销视图，而 `Shutdown` 正是驱动那次卸出的地方。")
D.SetAccess("public")
D.Interface("FGameWorld()", "构造：**不声明任何前向依赖** —— 世界只靠自己与装进来的系统，"
            "对可选的引擎插件 `WaitFor` 会让图编译失败。唯一的边是上面那条反向边，"
            "且按名字声明（世界是通用脚手架，不该 build 依赖可选的 UI 插件）")
D.Interface("~FGameWorld() override", "析构：**什么都不做**。图与组件池都在 `Shutdown` 里收掉 —— "
            "那时世界系统还都活着；把清空留给析构就等于让池先于系统释放，是跨模块的硬 AV")
D.Interface("[[nodiscard]] FEntity CreateEntity()", "创建实体（转发到 `Registry.Create()`）；"
            "返回的句柄要跨帧持有就必须复核 `IsAlive`")
D.Interface("bool DestroyEntity(FEntity E)", "销毁实体并**逐个池清掉该 Index 的槽**。"
            "顺序上先 `Registry.Destroy` 校验代号，通过才动组件 —— 陈旧句柄不会误删别人的组件")
D.Interface("[[nodiscard]] bool IsAlive(FEntity E) const", "实体活性（代号 + 存活位），转发给注册表")
D.Interface("[[nodiscard]] FEntityRegistry& GetRegistry()", "暴露注册表，供系统做容量 / 遍历等底层操作")
D.Interface("template <typename C> C* AddComponent(FEntity E, C Value)",
            "挂组件：`GetOrAddPool<C>()`（没有就建池）→ `Add(E.Index, move(Value))`。"
            "返回池内地址；**地址稳定**（池不压缩），但池扩容会让旧 `T*` 之外的一切失效")
D.Interface("template <typename C> [[nodiscard]] C* GetComponent(FEntity E)",
            "取组件：池不存在或该槽为空都返回 nullptr（不建池）")
D.Interface("template <typename C> void RemoveComponent(FEntity E)",
            "标记该实体的组件槽为空（池不存在时静默跳过）")
D.Interface("template <typename C> std::vector<FEntity> GetAllWithComponent()",
            "列出**所有**带类型 C 组件的存活槽 —— 不区分是哪个系统创建的。"
            "渲染器/可视化器需要一个完整集合（例如所有 `FUIWidget`）时用它，"
            "否则每个系统只能看到自己的实体。遍历上界是 `Pool->Capacity()`，"
            "返回的 `FEntity` 代号填 0（此处只按 Index 寻址，不校验代）")
D.Interface("void SetFixedStep(float Seconds)", "设置固定步长（秒）；改小时会让累加器一次跑更多步")
D.Interface("[[nodiscard]] float GetFixedStep() const", "当前固定步长（默认 1/60 s）")
D.Interface("[[nodiscard]] float GetDeltaSeconds() const", "上一帧的真实经过时间（固定步进累加用它）")
D.SetAccess("private")
D.Interface("void PreInitialize(FEngineBase&) override / void PostInitialize(...) override",
            "空实现：stage 序列是编译期类型，子图在首次使用时才建，没有东西要在 init 阶段准备")
D.Interface("void Initialize(FEngineBase&) override",
            "登记全局访问器、起表计时、把累加器清零，做一次对象模型自检"
            "（建实体 → 挂 `FTransform` → 读回），最后 `Install<FUISystem>()` 装入世界系统")
D.Interface("void BeginFrame(FEngineBase&) override / void EndFrame(...) override / void RequestExit(...) override",
            "空实现：世界不在引擎的帧边界做事，一切都发生在 `Tick` 里")
D.Interface("void Tick(FEngineBase&) override",
            "世界的一帧，四步：① `Wait()` 帧首栅栏（上一帧的 post 组是**不等**就派发的，在这里被收掉，"
            "同时保证后面改帧集时没有活节点在路上 —— 改动一个在使用中的帧集 = 用已释放的节点）；"
            "② `FlushPendingUpdates<IOnInstalled, IPreUnInstall>()` **安全点**（系统装卸唯一发生的地方，"
            "此刻图上空，收集器自己会发现帧集变了并重查）；③ 算 dt；"
            "④ `Execute<FInputStages>()` + 立即 `Wait`、`Execute<FFixedStages>()` + 每步 `Wait`、"
            "再 `Execute<FPostStages>()`（**不 trailing wait** ⇒ 与下一帧帧首 `Wait` 流水）")
D.Interface("void PreShutdown(FEngineBase&) override / void PostShutdown(...) override",
            "空实现：真正的排空在 `Shutdown` 里，两侧不需要额外的钩子")
D.Interface("void Shutdown(FEngineBase&) override",
            "顺序是这条函数的关键：① `Wait()` 排空；② **先清自己的 `ComponentPools`** —— "
            "池里的对象的析构函数活在世界系统的模块里，图的节点也指着它们的实例，"
            "晚一步就会在已卸载的模块里跑代码（硬 AV，不是泄漏）；"
            "③ 再 `UninstallAll()` + `FlushPendingUpdates` 逐个卸出系统 —— **走上 teardown 流水线**，"
            "每个系统的 `IPreUnInstall` 会在实例销毁前跑（裸的成员析构会跳过它、漏掉跨模块订阅）；"
            "**按名字**卸出，因为名字比模块活得久；④ 清掉全局访问器")
D.Interface("template <typename C> TComponentPool<C>* GetOrAddPool()",
            "私有：在异构池集合里按 `dynamic_cast` 找 C 的池，没有就新建再返回裸指针。"
            "**类型擦除的代价就在这里**：世界不是类模板，只能用 RTTI 找回类型")
D.Interface("template <typename C> TComponentPool<C>* GetPool()",
            "私有：只查找不创建（`GetComponent` / `RemoveComponent` / `GetAllWithComponent` 都走它）")
D.Nested("FInputStages = TTypeList<IProcessInput>", Kind="alias",
         Desc="输入组的 stage 序列（窄序列：只含本组的 stage）")
D.Nested("FFixedStages = TTypeList<IFixedUpdate>", Kind="alias", Desc="固定步进组的 stage 序列")
D.Nested("FPostStages = TTypeList<IUpdate, ILateUpdate>", Kind="alias",
         Desc="帧末组的 stage 序列 —— `IUpdate` / `ILateUpdate` 同序列即自带先后边")
D.Field("FEntityRegistry Registry", "实体身份注册表（世界的对象模型本体）")
D.Field("std::vector<std::unique_ptr<IComponentPool>> ComponentPools",
        "每种组件类型一个池（类型擦除的异构集合）。声明为**类内成员** ⇒ 生命周期跟着世界；"
        "清空只发生在 `Shutdown`，那时世界系统还在")
D.Field("std::chrono::steady_clock::time_point LastFrame", "上一帧的时间戳（`DeltaSeconds` 的基准）")
D.Field("float DeltaSeconds = 0.f", "上一帧真实经过时间")
D.Field("float FixedStepSeconds = 1.f / 60.f", "固定步长（默认 60 Hz）")
D.Field("float Accumulator = 0.f", "未消费的剩余时间：每帧累加 dt，够一步就消费一步（余数留到下一帧）")

D.Card("跨 DLL 导出面")
D.Table("符号", "说明")
D.Row("Maho::GameWorld::FGameWorld", "世界帧本体（宿主构造 / 析构它 ⇒ 必须导出）")
D.Row("Maho::GameWorld::<6 个 ECS stage 接口>", "世界系统在自己模块里继承它们 ⇒ 必须导出，否则派生类的 vtable 会在错误模块里落地")
D.Row("Maho::GameWorld::FTransform", "只是示例组件；跨模块按值使用时同样受导出规则约束")
D.Row("CreateFrame()（`Private/GameWorld.cpp`）", "宿主按符号名查找的 C 导出，转发到 `FGameWorld::CreateFrame()`")
