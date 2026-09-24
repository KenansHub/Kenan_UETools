#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "KenanSmoothNormalBakerSettings.generated.h"

UENUM()
enum class EKenanSmoothNormalOutputMode : uint8
{
	CreateDuplicate UMETA(DisplayName = "Create Duplicate（默认）"),
	OverwriteSource UMETA(DisplayName = "Overwrite Source（危险）")
};

UENUM()
enum class EKenanSmoothNormalLODMode : uint8
{
	AllImportedLODs UMETA(DisplayName = "全部可编辑 LOD"),
	SpecificLOD UMETA(DisplayName = "指定 LOD")
};

UCLASS(Transient)
class UKenanSmoothNormalBakerSettings final : public UObject
{
	GENERATED_BODY()

public:
	UKenanSmoothNormalBakerSettings();

	UPROPERTY(EditAnywhere, Category = "输入", meta = (AllowedClasses = "/Script/Engine.StaticMesh,/Script/Engine.SkeletalMesh"))
	TArray<TSoftObjectPtr<UObject>> TargetAssets;

	UPROPERTY(EditAnywhere, Category = "处理")
	EKenanSmoothNormalLODMode LODMode = EKenanSmoothNormalLODMode::AllImportedLODs;

	UPROPERTY(EditAnywhere, Category = "处理", meta = (ClampMin = "0", EditCondition = "LODMode == EKenanSmoothNormalLODMode::SpecificLOD", EditConditionHides))
	int32 SpecificLOD = 0;

	UPROPERTY(EditAnywhere, Category = "处理", DisplayName = "写入 UV 通道（Destination UV Channel）", meta = (ClampMin = "0", ClampMax = "7"))
	int32 DestinationUVChannel = 3;

	UPROPERTY(EditAnywhere, Category = "处理", meta = (ClampMin = "0.000001", ClampMax = "0.1", UIMin = "0.00001", UIMax = "0.01"))
	float WeldTolerance = 0.0001f;

	UPROPERTY(EditAnywhere, Category = "处理", DisplayName = "计算曲率（仅报告，不写入）")
	bool bComputeCurvatureForReport = true;

	UPROPERTY(VisibleAnywhere, Category = "处理")
	FString Encoding = TEXT("Octahedral Float2 [0,1]");

	UPROPERTY(EditAnywhere, Category = "输出")
	EKenanSmoothNormalOutputMode OutputMode = EKenanSmoothNormalOutputMode::CreateDuplicate;

	UPROPERTY(EditAnywhere, Category = "输出", meta = (EditCondition = "OutputMode == EKenanSmoothNormalOutputMode::CreateDuplicate"))
	FString DuplicateSuffix = TEXT("_SmoothNormal");

	UPROPERTY(EditAnywhere, Category = "输出", meta = (EditCondition = "OutputMode == EKenanSmoothNormalOutputMode::CreateDuplicate"))
	FString DuplicatePackagePath;

	UPROPERTY(EditAnywhere, Category = "输出")
	bool bSaveOutputAsset = true;
};
