#pragma once

#include "CoreMinimal.h"
#include "KenanToolSnapshot.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IKenanEditorTool;
class IModularFeature;
class SBox;
class SSearchBox;

class SKenanToolsPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKenanToolsPanel) {}
	SLATE_END_ARGS()

	virtual ~SKenanToolsPanel() override;
	void Construct(const FArguments& InArgs);

private:
	void RefreshTools();
	void HandleModularFeatureRegistered(const FName& FeatureName, IModularFeature* Feature);
	void HandleModularFeatureUnregistered(const FName& FeatureName, IModularFeature* Feature);
	void HandleSearchTextChanged(const FText& NewText);
	void HandleToolSelectionChanged(TSharedPtr<FKenanToolListItem> Item, ESelectInfo::Type SelectInfo);
	TSharedRef<ITableRow> GenerateToolRow(TSharedPtr<FKenanToolListItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<SWidget> BuildEmptyState(const FText& Message) const;
	FText GetToolCountText() const;
	FText GetDiagnosticsText() const;

	TArray<TSharedPtr<FKenanToolListItem>> ToolItems;
	TSharedPtr<SListView<TSharedPtr<FKenanToolListItem>>> ToolListView;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SBox> ToolContentBox;
	FString FilterText;
	FName CurrentToolId;
	IKenanEditorTool* CurrentProvider = nullptr;
	FText DiagnosticsText;
	FDelegateHandle FeatureRegisteredHandle;
	FDelegateHandle FeatureUnregisteredHandle;
};
