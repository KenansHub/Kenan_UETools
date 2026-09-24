#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Features/IModularFeatures.h"
#include "IKenanEditorTool.h"
#include "KenanToolSnapshot.h"

namespace Kenan::Tests
{
class FFakeEditorTool final : public IKenanEditorTool
{
public:
	FFakeEditorTool(FName InToolId, FName InCategory, int32 InSortOrder, const TCHAR* InDisplayName)
		: ToolId(InToolId)
		, Category(InCategory)
		, SortOrder(InSortOrder)
		, DisplayName(FText::FromString(InDisplayName))
	{
	}

	virtual FName GetToolId() const override { return ToolId; }
	virtual FText GetDisplayName() const override { return DisplayName; }
	virtual FText GetDescription() const override { return FText::FromString(TEXT("Automation test provider")); }
	virtual FName GetCategory() const override { return Category; }
	virtual int32 GetSortOrder() const override { return SortOrder; }
	virtual TSharedPtr<SWidget> CreateWidget() override { return nullptr; }

private:
	FName ToolId;
	FName Category;
	int32 SortOrder;
	FText DisplayName;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKenanToolModularFeatureTest,
	"KenanTools.Host.ModularFeatureLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKenanToolModularFeatureTest::RunTest(const FString& Parameters)
{
	using namespace Kenan::Tests;

	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	const FName FeatureName = IKenanEditorTool::GetModularFeatureName();
	FFakeEditorTool Alpha(TEXT("KenanTest.Alpha"), TEXT("Mesh"), 20, TEXT("Alpha"));
	FFakeEditorTool Beta(TEXT("KenanTest.Beta"), TEXT("Mesh"), 10, TEXT("Beta"));
	FFakeEditorTool DuplicateA(TEXT("KenanTest.Duplicate"), TEXT("Mesh"), 30, TEXT("Duplicate A"));
	FFakeEditorTool DuplicateB(TEXT("KenanTest.Duplicate"), TEXT("Mesh"), 40, TEXT("Duplicate B"));

	int32 RegisteredEvents = 0;
	int32 UnregisteredEvents = 0;
	const FDelegateHandle RegisteredHandle = ModularFeatures.OnModularFeatureRegistered().AddLambda(
		[&](const FName& RegisteredName, IModularFeature* Feature)
		{
			if (RegisteredName == FeatureName
				&& (Feature == &Alpha || Feature == &Beta || Feature == &DuplicateA || Feature == &DuplicateB))
			{
				++RegisteredEvents;
			}
		});
	const FDelegateHandle UnregisteredHandle = ModularFeatures.OnModularFeatureUnregistered().AddLambda(
		[&](const FName& UnregisteredName, IModularFeature* Feature)
		{
			if (UnregisteredName == FeatureName
				&& (Feature == &Alpha || Feature == &Beta || Feature == &DuplicateA || Feature == &DuplicateB))
			{
				++UnregisteredEvents;
			}
		});

	ModularFeatures.RegisterModularFeature(FeatureName, &Alpha);
	ModularFeatures.RegisterModularFeature(FeatureName, &Beta);
	ModularFeatures.RegisterModularFeature(FeatureName, &DuplicateA);
	ModularFeatures.RegisterModularFeature(FeatureName, &DuplicateB);

	TArray<IKenanEditorTool*> RegisteredProviders = ModularFeatures.GetModularFeatureImplementations<IKenanEditorTool>(FeatureName);
	TestTrue(TEXT("Alpha is discoverable while registered"), RegisteredProviders.Contains(&Alpha));
	TestTrue(TEXT("Beta is discoverable while registered"), RegisteredProviders.Contains(&Beta));
	TArray<IKenanEditorTool*> Providers = { &Alpha, &Beta, &DuplicateA, &DuplicateB };
	TArray<TSharedPtr<FKenanToolListItem>> Items;
	FKenanToolSnapshotDiagnostics Diagnostics;
	FKenanToolSnapshot::Build(Providers, FString(), Items, Diagnostics);

	TestEqual(TEXT("All four test providers emit registration events"), RegisteredEvents, 4);
	TestEqual(TEXT("Duplicate providers are both hidden"), Items.Num(), 2);
	if (Items.Num() == 2)
	{
		TestEqual(TEXT("Sort order places Beta first"), Items[0]->ToolId, FName(TEXT("KenanTest.Beta")));
		TestEqual(TEXT("Sort order places Alpha second"), Items[1]->ToolId, FName(TEXT("KenanTest.Alpha")));
	}
	TestEqual(TEXT("One duplicate ToolId is diagnosed"), Diagnostics.DuplicateToolIds.Num(), 1);
	if (Diagnostics.DuplicateToolIds.Num() == 1)
	{
		TestEqual(TEXT("Duplicate ToolId is preserved in diagnostics"), Diagnostics.DuplicateToolIds[0], FName(TEXT("KenanTest.Duplicate")));
	}

	ModularFeatures.UnregisterModularFeature(FeatureName, &DuplicateB);
	ModularFeatures.UnregisterModularFeature(FeatureName, &DuplicateA);
	ModularFeatures.UnregisterModularFeature(FeatureName, &Beta);
	ModularFeatures.UnregisterModularFeature(FeatureName, &Alpha);
	TestEqual(TEXT("All four test providers emit unregistration events"), UnregisteredEvents, 4);

	RegisteredProviders = ModularFeatures.GetModularFeatureImplementations<IKenanEditorTool>(FeatureName);
	TestFalse(TEXT("Alpha is absent after unregister"), RegisteredProviders.Contains(&Alpha));
	TestFalse(TEXT("Beta is absent after unregister"), RegisteredProviders.Contains(&Beta));
	TestFalse(TEXT("Duplicate A is absent after unregister"), RegisteredProviders.Contains(&DuplicateA));
	TestFalse(TEXT("Duplicate B is absent after unregister"), RegisteredProviders.Contains(&DuplicateB));

	ModularFeatures.OnModularFeatureRegistered().Remove(RegisteredHandle);
	ModularFeatures.OnModularFeatureUnregistered().Remove(UnregisteredHandle);
	return true;
}

#endif
