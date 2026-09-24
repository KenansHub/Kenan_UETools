#include "Tools/SmoothNormalBaker/Editor/SKenanSmoothNormalBakerPanel.h"

#include "Tools/SmoothNormalBaker/Editor/KenanSmoothNormalMeshAdapter.h"
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "Tools/SmoothNormalBaker/Editor/KenanSmoothNormalBakerSettings.h"
#include "Misc/EngineVersion.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SKenanSmoothNormalBakerPanel"

void SKenanSmoothNormalBakerPanel::Construct(const FArguments& InArgs)
{
	Settings = TStrongObjectPtr<UKenanSmoothNormalBakerSettings>(NewObject<UKenanSmoothNormalBakerSettings>());

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bLockable = false;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.ViewIdentifier = TEXT("KenanTools.SmoothNormalBaker.Settings");
	DetailsView = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
	DetailsView->SetObject(Settings.Get());

	ChildSlot
	[
		SNew(SBorder)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ToolTitle", "SmoothNormalBaker"))
				.Font(FAppStyle::GetFontStyle(TEXT("HeadingLarge")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text(FText::Format(
					LOCTEXT("Compatibility", "公开 MeshDescription API；源码目标 UE5.4+；当前运行环境 {0}"),
					FText::FromString(FEngineVersion::Current().ToString(EVersionComponent::Patch))))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(FMargin(12.0f, 4.0f))
					.Text(LOCTEXT("ReadSelection", "从内容浏览器读取选择"))
					.ToolTipText(LOCTEXT("ReadSelectionTooltip", "读取内容浏览器中选中的 Static Mesh 或 Skeletal Mesh，并忽略不支持的资产类型。"))
					.OnClicked(this, &SKenanSmoothNormalBakerPanel::ReadContentBrowserSelection)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(this, &SKenanSmoothNormalBakerPanel::GetSelectionSummary)
			]
			+ SVerticalBox::Slot()
			.FillHeight(0.58f)
			[
				DetailsView.ToSharedRef()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(FMargin(12.0f, 4.0f))
					.Text(LOCTEXT("Preflight", "预检（不修改）"))
					.ToolTipText(LOCTEXT("PreflightTooltip", "只分析目标资产和当前设置；不创建副本、不修改资产、不执行保存。"))
					.OnClicked(this, &SKenanSmoothNormalBakerPanel::RunPreflight)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(FMargin(12.0f, 4.0f))
					.Text(LOCTEXT("Bake", "烘焙"))
					.ToolTipText(LOCTEXT("BakeTooltip", "按当前设置写入目标 UV 通道。默认生成副本；选择原地覆盖时会再次要求确认。"))
					.OnClicked(this, &SKenanSmoothNormalBakerPanel::RunBake)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(0.42f)
			[
				SAssignNew(ReportTextBox, SMultiLineEditableTextBox)
				.IsReadOnly(true)
				.AutoWrapText(true)
				.Text(LOCTEXT("InitialReport", "请选择 Static Mesh 或 Skeletal Mesh，然后先执行预检。"))
			]
		]
	];
}

FReply SKenanSmoothNormalBakerPanel::ReadContentBrowserSelection()
{
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> SelectedAssets;
	ContentBrowser.Get().GetSelectedAssets(SelectedAssets);
	Settings->TargetAssets.Reset();
	for (const FAssetData& AssetData : SelectedAssets)
	{
		UObject* Asset = AssetData.GetAsset();
		if (FKenanSmoothNormalMeshAdapter::IsSupportedAsset(Asset))
		{
			Settings->TargetAssets.Add(Asset);
		}
	}
	DetailsView->ForceRefresh();
	SetReport(FString::Printf(TEXT("已读取 %d 个受支持资产。"), Settings->TargetAssets.Num()));
	return FReply::Handled();
}

FReply SKenanSmoothNormalBakerPanel::RunPreflight()
{
	const TArray<UObject*> Assets = ResolveTargetAssets();
	if (Assets.IsEmpty())
	{
		SetReport(TEXT("PRECHECK FAILED：没有有效的 Static Mesh 或 Skeletal Mesh。"));
		return FReply::Handled();
	}

	TArray<FString> Reports;
	for (UObject* Asset : Assets)
	{
		Reports.Add(FKenanSmoothNormalMeshAdapter::Preflight(Asset, *Settings).Summary);
	}
	SetReport(FString::Join(Reports, TEXT("\n\n----------------\n\n")));
	return FReply::Handled();
}

FReply SKenanSmoothNormalBakerPanel::RunBake()
{
	const TArray<UObject*> Assets = ResolveTargetAssets();
	if (Assets.IsEmpty())
	{
		SetReport(TEXT("BAKE FAILED：没有有效的 Static Mesh 或 Skeletal Mesh。"));
		return FReply::Handled();
	}

	if (Settings->OutputMode == EKenanSmoothNormalOutputMode::OverwriteSource)
	{
		const FText Warning = FText::Format(
			LOCTEXT("OverwriteWarning", "将原地覆盖 {0} 个源资产的 UV{1}。该操作会直接修改并保存源资产，且难以批量回滚。是否继续？"),
			FText::AsNumber(Assets.Num()),
			FText::AsNumber(Settings->DestinationUVChannel));
		if (FMessageDialog::Open(EAppMsgType::YesNo, Warning, LOCTEXT("OverwriteTitle", "确认 Overwrite Source")) != EAppReturnType::Yes)
		{
			SetReport(TEXT("已取消原地覆盖。"));
			return FReply::Handled();
		}
	}

	TArray<FString> Reports;
	bool bAllSucceeded = true;
	for (UObject* Asset : Assets)
	{
		const FKenanAssetOperationReport Report = FKenanSmoothNormalMeshAdapter::Bake(Asset, *Settings);
		Reports.Add(Report.Summary);
		bAllSucceeded &= Report.bSucceeded;
	}
	SetReport(FString::Join(Reports, TEXT("\n\n----------------\n\n")));

	FNotificationInfo Notification(bAllSucceeded
		? LOCTEXT("BakeSuccess", "SmoothNormalBaker 完成。")
		: LOCTEXT("BakeFailure", "SmoothNormalBaker 存在失败或未保存结果，请查看报告。"));
	Notification.ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().AddNotification(Notification);
	return FReply::Handled();
}

FText SKenanSmoothNormalBakerPanel::GetSelectionSummary() const
{
	return FText::Format(LOCTEXT("SelectionSummary", "有效目标：{0}"), FText::AsNumber(ResolveTargetAssets().Num()));
}

void SKenanSmoothNormalBakerPanel::SetReport(const FString& Text)
{
	if (ReportTextBox.IsValid())
	{
		ReportTextBox->SetText(FText::FromString(Text));
	}
}

TArray<UObject*> SKenanSmoothNormalBakerPanel::ResolveTargetAssets() const
{
	TArray<UObject*> Assets;
	if (!Settings.IsValid())
	{
		return Assets;
	}
	for (const TSoftObjectPtr<UObject>& AssetPtr : Settings->TargetAssets)
	{
		UObject* Asset = AssetPtr.LoadSynchronous();
		if (FKenanSmoothNormalMeshAdapter::IsSupportedAsset(Asset))
		{
			Assets.AddUnique(Asset);
		}
	}
	return Assets;
}

#undef LOCTEXT_NAMESPACE
