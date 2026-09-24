# Kenan Tools 架构与扩展指南

## 目标

`Kenan_Tools` 是轻量、可迁移的 Unreal Editor 工具宿主。插件只保留一个顶层入口 `Tools -> Kenan Tools`；具体工具通过统一扩展接口注册，并在窗口中动态发现。新增工具不应修改宿主窗口，也不应把具体工具的引擎模块依赖堆进宿主逻辑。

首轮源码目标为 UE 5.4–5.6。本文同时记录了对 UE 4.27、5.2、5.4、5.6 官方 `IModularFeature / IModularFeatures` 核心接口的源码核对结果，但未经对应版本真实编译，不标记为已验证兼容。

## 设计依据与取舍

架构参考 Epic 官方源码中的轻量自注册方式：

- Modeling Mode 的 Tool Extension 以 `IModularFeature` 暴露扩展；
- Modeling Tools 宿主通过 `GetModularFeatureImplementations` 枚举 Provider；
- Hair Modeling Toolset 在模块 Startup／Shutdown 中成对注册和注销；
- Editor System Config Assistant 监听 Modular Feature 注册／注销事件刷新已打开界面。

Kenan Tools 只复用注册、发现和生命周期模式，不引入 Interactive Tools Framework、Tool Builder、Command Palette、配置资产扫描或 UObject 反射发现。这样可以维持即插即用，同时避免为简单批处理工具承担过重框架成本。

跨版本公共交集只使用：

- `RegisterModularFeature`
- `UnregisterModularFeature`
- `GetModularFeatureImplementations`
- `OnModularFeatureRegistered`
- `OnModularFeatureUnregistered`

不使用 UE 5.2+ 才出现的 `FScopedLockModularFeatureList`。注册、注销和界面刷新都必须在 Game Thread 发生。

## 总体结构

```text
Tools -> Kenan Tools
        |
        v
SKenanToolsPanel (宿主：搜索、排序、选择、空状态、诊断)
        |
        v
IModularFeatures["KenanEditorTool"]
        |
        +-- FKenanSmoothNormalBakerTool
        |      +-- SKenanSmoothNormalBakerPanel
        |      +-- Settings / Core / AssetAdapters / Compatibility
        |
        +-- FutureToolProvider (可位于同模块或独立 Editor 模块)
               +-- FutureToolWidget
```

宿主只依赖公开的 `IKenanEditorTool` 合同。它不知道具体工具处理 Mesh、材质还是动画，也不持有工具设置对象。

## 公共扩展接口

入口位于 `Source/KenanTools/Public/IKenanEditorTool.h`：

```cpp
class IKenanEditorTool : public IModularFeature
{
public:
    virtual FName GetToolId() const = 0;
    virtual FText GetDisplayName() const = 0;
    virtual FText GetDescription() const = 0;
    virtual FName GetCategory() const = 0;
    virtual int32 GetSortOrder() const = 0;
    virtual TSharedPtr<SWidget> CreateWidget() = 0;
};
```

字段合同：

- `ToolId`：稳定、不可本地化、不可为空；同一进程内必须唯一。
- `DisplayName`：用户可见名称，可本地化。
- `Description`：一句话说明用途与输入范围，用于工具列表和搜索。
- `Category`：稳定类别 ID；首版只用于显示和排序。
- `SortOrder`：同类别内的小值优先顺序。
- `CreateWidget`：选中工具时按需创建界面；失败可返回空指针，由宿主显示错误状态。

宿主按 `Category -> SortOrder -> DisplayName -> ToolId` 稳定排序，并在名称、描述、类别和 ID 中搜索。

## 生命周期

Provider 必须在所属模块的 Startup／Shutdown 中成对注册：

```cpp
void FExampleModule::StartupModule()
{
    ToolProvider = MakeUnique<FExampleTool>();
    IModularFeatures::Get().RegisterModularFeature(
        IKenanEditorTool::GetModularFeatureName(),
        ToolProvider.Get());
}

void FExampleModule::ShutdownModule()
{
    if (ToolProvider)
    {
        IModularFeatures::Get().UnregisterModularFeature(
            IKenanEditorTool::GetModularFeatureName(),
            ToolProvider.Get());
        ToolProvider.Reset();
    }
}
```

Provider 对象必须比注册状态活得更久；先注销，再销毁。宿主监听官方注册／注销事件，工具卸载时会先移除当前 Widget 和裸 Provider 指针，然后刷新列表。

空 `ToolId` 的 Provider 不显示。若多个 Provider 使用相同 `ToolId`，宿主会隐藏全部冲突项并记录错误，不根据模块加载顺序选择其中一个。

## 目录职责

```text
Kenan_Tools/
  Docs/
    Architecture.md
  Source/KenanTools/
    Public/
      IKenanEditorTool.h             # 稳定的扩展 ABI 边界
      KenanToolsModule.h
    Private/
      SKenanToolsPanel.*             # 纯宿主
      KenanToolSnapshot.*            # 可测试的过滤、冲突诊断与排序
      Tests/
        KenanToolModularFeatureTests.cpp
      Tools/SmoothNormalBaker/
        KenanSmoothNormalBakerTool.* # Provider／Widget 工厂
        Core/                         # 必须：关键算法，不接触 UObject 资产或版本宏
          KenanSmoothNormalCore.*
        Editor/                       # UI、Settings、资产 Adapter 与保存
          KenanSmoothNormalMeshAdapter.*
          KenanSmoothNormalBakerSettings.*
          SKenanSmoothNormalBakerPanel.*
        Compat/                       # 按需：本工具真实存在的引擎版本差异
          KenanSmoothNormalBakerCompat.h
        Tests/                        # 按需：本工具的 Core／Editor 测试
          KenanSmoothNormalCoreTests.cpp
```

规则：

- 算法核心不引用项目资产、角色类型、Slate 或引擎私有头。
- 每个工具采用三层职责：`Core`、`Editor`、按需建立的 `Compat`；`Tests` 是按风险增加的验证目录，不算业务层。Provider 留在工具根目录。
- 引擎版本判断只进入该工具自己的 `Compat`。只有被两个以上工具证明共同需要、且语义完全一致的兼容能力，才允许提升到 `Private/Shared/Compat`。
- `Editor` 可以依赖 Core 与 Compat，负责 UI、Settings、UObject 资产适配和保存；Core 不得反向依赖 Editor、Compat 或 Provider。
- `Editor` 不实现关键算法；除展示当前引擎信息外，不直接出现版本判断。
- 宿主不 include 具体工具的 Settings、Adapter 或 Widget。
- 工具自己的失败报告留在工具内部；一个工具失败不能阻断其他工具。
- 不修改引擎源码，不依赖 Engine `Private` 头。

## 新增工具

### 轻量工具：留在 KenanTools 模块

1. 在 `Private/Tools/<ToolName>/` 添加 Provider，并按 `Core / Editor / Compat（按需） / Tests（按需）` 放置代码。
2. 先定义 Core 的输入、输出和纯计算测试，再由 Adapter 处理 UObject、资产复制、保存和版本兼容调用。
3. Provider 实现 `IKenanEditorTool`，使用唯一 `ToolId`。
4. 在 `FKenanToolsModule` 中持有 Provider，并在 Startup／Shutdown 成对注册／注销。
5. 为 Core、Compat 边界和高风险 Editor 资产行为添加对应测试。
6. 更新 README 的工具清单；如引入新的模块依赖，解释原因。

### 重依赖工具：独立 Editor 模块

1. 新模块只需要依赖 KenanTools 的公开接口和自身所需模块。
2. 在该模块内部持有并注册 Provider。
3. 不修改 `SKenanToolsPanel`，也不把重依赖加入 KenanTools 宿主。
4. 确保模块卸载前先注销 Provider。

只有在工具确实引入大型、互不相关的依赖或需要独立加载策略时才拆模块；不为每个小工具机械增加模块。

## SmoothNormalBaker 边界

`SmoothNormalBaker` 是第一个 Provider，但不是宿主特例。它继续保持：

- Static Mesh 与 Skeletal Mesh 共用焊接位置平滑算法；
- 平滑法线转换到原始切线空间后，以八面体 `float2` 写入可选 UV；
- 默认生成副本，原地覆盖必须二次确认；
- 曲率计算接口保留，v1 不落盘；因此只读取当前 UV 输出的消费材质无法用曲率补偿高曲率区域的视觉宽度不均。未来必须先明确独立通道、顶点色或重新打包方案，不能在没有存储合同的情况下隐式覆盖其他数据；
- 跨版本提交差异集中在 `Tools/SmoothNormalBaker/Compat/KenanSmoothNormalBakerCompat.h`，只包装 Skeletal Commit 参数差异，不承载 UV 上限或界面信息。

宿主重构不得改变这些计算和资产写入合同。

## 工具面板交互约定

- 动作按钮使用 UE 默认 `SButton` 视觉，不把单个按钮横向拉满整个内容列；优先放入左对齐 `SHorizontalBox` 的 `AutoWidth` Slot。
- 同一操作组中的按钮保持紧凑间距和一致内边距；完整宽度只留给确实需要整行输入的控件或状态区域。
- 每个会读取、分析、修改或保存资产的动作按钮必须提供 Tooltip，说明输入范围、是否修改资产、是否保存，以及是否会触发覆盖确认。
- 按钮文字描述动作；静态说明使用 `STextBlock`，不得依靠满宽按钮形态充当 Label。

## 验证层级

1. 静态检查：目录、依赖、版本宏、注册／注销成对、文档入口。
2. C++ 编译：目标引擎构建成功；这一步开始前需要显式确认编辑器已关闭。
3. 自动化测试：核心八面体编码、焊接连续性、Provider 排序、重复 ID 与注销事件。
4. 界面验证：单一 Tools 菜单入口、动态列表、搜索、重开窗口不重复注册。
5. 资产验证：先用 Static Mesh 副本，再测 Skeletal Mesh；检查目标 UV、保存重载和回滚。
6. 视觉验收：最后使用独立测试副本对比尖端与高曲率区域的轮廓连续性和宽度；技术成功不代替视觉结论。

当前源码已在 UE 5.6.1 完成 UHT／UBT、核心算法、真实 Static Mesh 重烘焙、独立冷加载和窗口加载验证；这只证明 UE 5.6.1 当前工程可用。UE 5.4、5.5 以及其他版本仍需各自实际编译和资产回归，不能仅凭兼容层结构声明已验证兼容。
