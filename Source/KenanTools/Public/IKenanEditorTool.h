#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

class SWidget;

/**
 * Minimal extension point for tools hosted by the Kenan Tools window.
 * Providers register and unregister through IModularFeatures during module startup/shutdown.
 */
class KENANTOOLS_API IKenanEditorTool : public IModularFeature
{
public:
	virtual ~IKenanEditorTool() = default;

	virtual FName GetToolId() const = 0;
	virtual FText GetDisplayName() const = 0;
	virtual FText GetDescription() const = 0;
	virtual FName GetCategory() const = 0;
	virtual int32 GetSortOrder() const = 0;
	virtual TSharedPtr<SWidget> CreateWidget() = 0;

	static FName GetModularFeatureName()
	{
		static const FName FeatureName(TEXT("KenanEditorTool"));
		return FeatureName;
	}
};
