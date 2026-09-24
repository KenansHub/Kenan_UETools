#pragma once

#include "CoreMinimal.h"

class IKenanEditorTool;

struct FKenanToolListItem
{
	IKenanEditorTool* Provider = nullptr;
	FName ToolId;
	FName Category;
	FText DisplayName;
	FText Description;
	int32 SortOrder = 0;
};

struct FKenanToolSnapshotDiagnostics
{
	int32 InvalidProviderCount = 0;
	TArray<FName> DuplicateToolIds;
};

class FKenanToolSnapshot
{
public:
	static void Build(
		const TArray<IKenanEditorTool*>& Providers,
		const FString& FilterText,
		TArray<TSharedPtr<FKenanToolListItem>>& OutItems,
		FKenanToolSnapshotDiagnostics& OutDiagnostics);
};
