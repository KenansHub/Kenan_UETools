#include "KenanToolSnapshot.h"

#include "IKenanEditorTool.h"

void FKenanToolSnapshot::Build(
	const TArray<IKenanEditorTool*>& Providers,
	const FString& FilterText,
	TArray<TSharedPtr<FKenanToolListItem>>& OutItems,
	FKenanToolSnapshotDiagnostics& OutDiagnostics)
{
	OutItems.Reset();
	OutDiagnostics = FKenanToolSnapshotDiagnostics();

	TMap<FName, int32> IdCounts;
	for (IKenanEditorTool* Provider : Providers)
	{
		if (!Provider || Provider->GetToolId().IsNone())
		{
			++OutDiagnostics.InvalidProviderCount;
			continue;
		}
		IdCounts.FindOrAdd(Provider->GetToolId())++;
	}

	for (const TPair<FName, int32>& Pair : IdCounts)
	{
		if (Pair.Value > 1)
		{
			OutDiagnostics.DuplicateToolIds.Add(Pair.Key);
		}
	}
	OutDiagnostics.DuplicateToolIds.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	const FString NormalizedFilter = FilterText.TrimStartAndEnd().ToLower();
	for (IKenanEditorTool* Provider : Providers)
	{
		if (!Provider)
		{
			continue;
		}

		const FName ToolId = Provider->GetToolId();
		const int32* Count = IdCounts.Find(ToolId);
		if (ToolId.IsNone() || !Count || *Count != 1)
		{
			continue;
		}

		const FText DisplayName = Provider->GetDisplayName();
		const FText Description = Provider->GetDescription();
		const FName Category = Provider->GetCategory();
		if (!NormalizedFilter.IsEmpty())
		{
			const FString Searchable = FString::Printf(
				TEXT("%s %s %s %s"),
				*ToolId.ToString(),
				*Category.ToString(),
				*DisplayName.ToString(),
				*Description.ToString()).ToLower();
			if (!Searchable.Contains(NormalizedFilter))
			{
				continue;
			}
		}

		TSharedPtr<FKenanToolListItem> Item = MakeShared<FKenanToolListItem>();
		Item->Provider = Provider;
		Item->ToolId = ToolId;
		Item->Category = Category;
		Item->DisplayName = DisplayName;
		Item->Description = Description;
		Item->SortOrder = Provider->GetSortOrder();
		OutItems.Add(MoveTemp(Item));
	}

	OutItems.Sort([](const TSharedPtr<FKenanToolListItem>& A, const TSharedPtr<FKenanToolListItem>& B)
	{
		const int32 CategoryCompare = A->Category.ToString().Compare(B->Category.ToString(), ESearchCase::IgnoreCase);
		if (CategoryCompare != 0)
		{
			return CategoryCompare < 0;
		}
		if (A->SortOrder != B->SortOrder)
		{
			return A->SortOrder < B->SortOrder;
		}
		const int32 DisplayCompare = A->DisplayName.ToString().Compare(B->DisplayName.ToString(), ESearchCase::IgnoreCase);
		if (DisplayCompare != 0)
		{
			return DisplayCompare < 0;
		}
		return A->ToolId.LexicalLess(B->ToolId);
	});
}
