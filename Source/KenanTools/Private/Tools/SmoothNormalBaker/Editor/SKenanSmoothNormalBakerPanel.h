#pragma once

#include "CoreMinimal.h"
#include "Tools/SmoothNormalBaker/Editor/KenanSmoothNormalBakerSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class SMultiLineEditableTextBox;
class SKenanSmoothNormalBakerPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SKenanSmoothNormalBakerPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply ReadContentBrowserSelection();
	FReply RunPreflight();
	FReply RunBake();
	FText GetSelectionSummary() const;
	void SetReport(const FString& Text);
	TArray<UObject*> ResolveTargetAssets() const;

	TStrongObjectPtr<UKenanSmoothNormalBakerSettings> Settings;
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SMultiLineEditableTextBox> ReportTextBox;
};
