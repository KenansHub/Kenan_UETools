#include "SKenanToolsPanel.h"

#include "Features/IModularFeatures.h"
#include "IKenanEditorTool.h"
#include "KenanToolsModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SKenanToolsPanel"

SKenanToolsPanel::~SKenanToolsPanel()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	if (FeatureRegisteredHandle.IsValid())
	{
		ModularFeatures.OnModularFeatureRegistered().Remove(FeatureRegisteredHandle);
	}
	if (FeatureUnregisteredHandle.IsValid())
	{
		ModularFeatures.OnModularFeatureUnregistered().Remove(FeatureUnregisteredHandle);
	}
}

void SKenanToolsPanel::Construct(const FArguments& InArgs)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	FeatureRegisteredHandle = ModularFeatures.OnModularFeatureRegistered().AddSP(
		this,
		&SKenanToolsPanel::HandleModularFeatureRegistered);
	FeatureUnregisteredHandle = ModularFeatures.OnModularFeatureUnregistered().AddSP(
		this,
		&SKenanToolsPanel::HandleModularFeatureUnregistered);

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)

		+ SSplitter::Slot()
		.Value(0.25f)
		[
			SNew(SBorder)
			.Padding(10.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ToolsTitle", "Kenan Tools"))
					.Font(FAppStyle::GetFontStyle(TEXT("HeadingMedium")))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 6.0f)
				[
					SAssignNew(SearchBox, SSearchBox)
					.HintText(LOCTEXT("SearchHint", "搜索工具"))
					.OnTextChanged(this, &SKenanToolsPanel::HandleSearchTextChanged)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Text(this, &SKenanToolsPanel::GetToolCountText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(ToolListView, SListView<TSharedPtr<FKenanToolListItem>>)
					.ListItemsSource(&ToolItems)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SKenanToolsPanel::GenerateToolRow)
					.OnSelectionChanged(this, &SKenanToolsPanel::HandleToolSelectionChanged)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SKenanToolsPanel::GetDiagnosticsText)
					.AutoWrapText(true)
					.ColorAndOpacity(FLinearColor(1.0f, 0.45f, 0.2f))
				]
			]
		]

		+ SSplitter::Slot()
		.Value(0.75f)
		[
			SAssignNew(ToolContentBox, SBox)
			[
				BuildEmptyState(LOCTEXT("LoadingTools", "正在发现工具……"))
			]
		]
	];

	RefreshTools();
}

void SKenanToolsPanel::RefreshTools()
{
	TArray<IKenanEditorTool*> Providers = IModularFeatures::Get().GetModularFeatureImplementations<IKenanEditorTool>(
		IKenanEditorTool::GetModularFeatureName());

	FKenanToolSnapshotDiagnostics Diagnostics;
	FKenanToolSnapshot::Build(Providers, FilterText, ToolItems, Diagnostics);

	TArray<FString> DiagnosticParts;
	if (Diagnostics.InvalidProviderCount > 0)
	{
		DiagnosticParts.Add(FString::Printf(TEXT("忽略 %d 个空 Provider 或空 ToolId。"), Diagnostics.InvalidProviderCount));
		UE_LOG(LogKenanTools, Error, TEXT("Ignored %d Kenan tool provider(s) with a null provider or empty ToolId."), Diagnostics.InvalidProviderCount);
	}
	for (const FName DuplicateToolId : Diagnostics.DuplicateToolIds)
	{
		DiagnosticParts.Add(FString::Printf(TEXT("重复 ToolId：%s（全部隐藏）"), *DuplicateToolId.ToString()));
		UE_LOG(LogKenanTools, Error, TEXT("Duplicate Kenan tool id '%s'; all conflicting providers are hidden."), *DuplicateToolId.ToString());
	}
	DiagnosticsText = FText::FromString(FString::Join(DiagnosticParts, TEXT("\n")));

	if (ToolListView.IsValid())
	{
		ToolListView->RequestListRefresh();
	}

	TSharedPtr<FKenanToolListItem> ItemToSelect;
	if (!CurrentToolId.IsNone())
	{
		for (const TSharedPtr<FKenanToolListItem>& Item : ToolItems)
		{
			if (Item.IsValid() && Item->ToolId == CurrentToolId)
			{
				ItemToSelect = Item;
				break;
			}
		}
	}
	if (!ItemToSelect.IsValid() && !ToolItems.IsEmpty())
	{
		ItemToSelect = ToolItems[0];
	}

	if (ItemToSelect.IsValid() && ToolListView.IsValid())
	{
		ToolListView->SetSelection(ItemToSelect, ESelectInfo::Direct);
	}
	else
	{
		CurrentToolId = NAME_None;
		CurrentProvider = nullptr;
		if (ToolListView.IsValid())
		{
			ToolListView->ClearSelection();
		}
		if (ToolContentBox.IsValid())
		{
			ToolContentBox->SetContent(BuildEmptyState(
				FilterText.IsEmpty()
					? LOCTEXT("NoTools", "没有已注册的 Kenan 工具。")
					: LOCTEXT("NoSearchResults", "没有符合搜索条件的工具。")));
		}
	}
}

void SKenanToolsPanel::HandleModularFeatureRegistered(const FName& FeatureName, IModularFeature* Feature)
{
	if (FeatureName == IKenanEditorTool::GetModularFeatureName())
	{
		RefreshTools();
	}
}

void SKenanToolsPanel::HandleModularFeatureUnregistered(const FName& FeatureName, IModularFeature* Feature)
{
	if (FeatureName != IKenanEditorTool::GetModularFeatureName())
	{
		return;
	}

	if (CurrentProvider == Feature)
	{
		CurrentProvider = nullptr;
		CurrentToolId = NAME_None;
		if (ToolContentBox.IsValid())
		{
			ToolContentBox->SetContent(BuildEmptyState(LOCTEXT("ToolUnregistered", "工具已注销，正在刷新列表……")));
		}
	}
	RefreshTools();
}

void SKenanToolsPanel::HandleSearchTextChanged(const FText& NewText)
{
	FilterText = NewText.ToString();
	RefreshTools();
}

void SKenanToolsPanel::HandleToolSelectionChanged(TSharedPtr<FKenanToolListItem> Item, ESelectInfo::Type SelectInfo)
{
	if (!ToolContentBox.IsValid())
	{
		return;
	}

	if (!Item.IsValid() || !Item->Provider)
	{
		CurrentToolId = NAME_None;
		CurrentProvider = nullptr;
		ToolContentBox->SetContent(BuildEmptyState(LOCTEXT("InvalidSelection", "所选工具无效。")));
		return;
	}

	const bool bKeepCurrentWidget = CurrentProvider == Item->Provider && CurrentToolId == Item->ToolId;
	CurrentToolId = Item->ToolId;
	CurrentProvider = Item->Provider;
	if (bKeepCurrentWidget)
	{
		return;
	}

	TSharedPtr<SWidget> ToolWidget = CurrentProvider->CreateWidget();
	if (ToolWidget.IsValid())
	{
		ToolContentBox->SetContent(ToolWidget.ToSharedRef());
	}
	else
	{
		UE_LOG(LogKenanTools, Error, TEXT("Kenan tool '%s' failed to create its widget."), *CurrentToolId.ToString());
		ToolContentBox->SetContent(BuildEmptyState(FText::Format(
			LOCTEXT("WidgetCreationFailed", "工具 {0} 无法创建界面，请查看日志。"),
			FText::FromName(CurrentToolId))));
	}
}

TSharedRef<ITableRow> SKenanToolsPanel::GenerateToolRow(
	TSharedPtr<FKenanToolListItem> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FKenanToolListItem>>, OwnerTable)
		.Padding(FMargin(5.0f, 4.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(Item.IsValid() ? Item->DisplayName : LOCTEXT("InvalidToolName", "无效工具"))
				.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Item.IsValid()
					? FText::Format(
						LOCTEXT("ToolRowDetail", "{0} · {1}"),
						FText::FromName(Item->Category),
						Item->Description)
					: FText::GetEmpty())
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

TSharedRef<SWidget> SKenanToolsPanel::BuildEmptyState(const FText& Message) const
{
	return SNew(SBorder)
		.Padding(24.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Message)
			.AutoWrapText(true)
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

FText SKenanToolsPanel::GetToolCountText() const
{
	return FText::Format(LOCTEXT("ToolCount", "可用工具：{0}"), FText::AsNumber(ToolItems.Num()));
}

FText SKenanToolsPanel::GetDiagnosticsText() const
{
	return DiagnosticsText;
}

#undef LOCTEXT_NAMESPACE
