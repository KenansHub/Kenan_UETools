#pragma once

#include "CoreMinimal.h"

class UObject;
class UKenanSmoothNormalBakerSettings;

struct FKenanAssetOperationReport
{
	bool bSucceeded = false;
	bool bSaved = false;
	TObjectPtr<UObject> ResultAsset = nullptr;
	FString Summary;
};

class FKenanSmoothNormalMeshAdapter
{
public:
	static bool IsSupportedAsset(const UObject* Asset);
	static int32 GetMaximumUVChannelCount(const UObject* Asset);

	static FKenanAssetOperationReport Preflight(
		UObject* SourceAsset,
		const UKenanSmoothNormalBakerSettings& Settings);

	static FKenanAssetOperationReport Bake(
		UObject* SourceAsset,
		const UKenanSmoothNormalBakerSettings& Settings);
};
