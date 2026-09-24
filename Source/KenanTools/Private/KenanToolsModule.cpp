#include "KenanToolsModule.h"

#include "Features/IModularFeatures.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "IKenanEditorTool.h"
#include "SKenanToolsPanel.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Tools/SmoothNormalBaker/KenanSmoothNormalBakerTool.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FKenanToolsModule"

DEFINE_LOG_CATEGORY(LogKenanTools);

namespace
{
	const FName KenanToolsTabName(TEXT("KenanTools"));
}

FKenanToolsModule::~FKenanToolsModule() = default;

void FKenanToolsModule::StartupModule()
{
	SmoothNormalBakerTool = MakeUnique<FKenanSmoothNormalBakerTool>();
	IModularFeatures::Get().RegisterModularFeature(
		IKenanEditorTool::GetModularFeatureName(),
		SmoothNormalBakerTool.Get());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		KenanToolsTabName,
		FOnSpawnTab::CreateRaw(this, &FKenanToolsModule::SpawnPluginTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Kenan Tools"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FKenanToolsModule::RegisterMenus));
}

void FKenanToolsModule::ShutdownModule()
{
	if (SmoothNormalBakerTool)
	{
		IModularFeatures::Get().UnregisterModularFeature(
			IKenanEditorTool::GetModularFeatureName(),
			SmoothNormalBakerTool.Get());
		SmoothNormalBakerTool.Reset();
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(KenanToolsTabName);
}

void FKenanToolsModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("KenanTools"), LOCTEXT("Section", "Kenan Tools"));
	Section.AddMenuEntry(
		TEXT("OpenKenanTools"),
		LOCTEXT("OpenWindow", "Kenan Tools"),
		LOCTEXT("OpenWindowTooltip", "打开 Kenan Tools 编辑器工具窗口。"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FKenanToolsModule::OpenPluginTab)));
}

void FKenanToolsModule::OpenPluginTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(KenanToolsTabName);
}

TSharedRef<SDockTab> FKenanToolsModule::SpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SKenanToolsPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FKenanToolsModule, KenanTools)
