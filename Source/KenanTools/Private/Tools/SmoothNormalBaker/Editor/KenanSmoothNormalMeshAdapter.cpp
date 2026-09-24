#include "Tools/SmoothNormalBaker/Editor/KenanSmoothNormalMeshAdapter.h"

#include "AssetToolsModule.h"
#include "Tools/SmoothNormalBaker/Compat/KenanSmoothNormalBakerCompat.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Tools/SmoothNormalBaker/Core/KenanSmoothNormalCore.h"
#include "Tools/SmoothNormalBaker/Editor/KenanSmoothNormalBakerSettings.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "StaticMeshOperations.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "KenanSmoothNormalMeshAdapter"

namespace Kenan::SmoothNormalBaker::Editor::Private
{
	inline constexpr int32 StaticMeshMaxUVChannels = 8;
	inline constexpr int32 SkeletalMeshMaxUVChannels = 4;

	struct FPreparedLOD
	{
		int32 LODIndex = INDEX_NONE;
		FMeshDescription MeshDescription;
		FKenanSmoothNormalMeshReport Report;
	};

	static FString FormatMeshReport(const int32 LODIndex, const FKenanSmoothNormalMeshReport& Report)
	{
		return FString::Printf(
			TEXT("LOD%d: V=%d, VI=%d, T=%d, WeldGroups=%d, UV %d->%d, Target=%s/%s, GeneratedBasis=%s, Degenerate=%d, InvalidBasis=%d [Normal=%d Tangent=%d Sign=%d DerivedB=%d], Encoded[Unique=%d Fallback=%d], HalfMax=%.6fdeg, Curvature[min=%.6f mean=%.6f max=%.6f]"),
			LODIndex,
			Report.VertexCount,
			Report.VertexInstanceCount,
			Report.TriangleCount,
			Report.WeldedGroupCount,
			Report.PreviousUVChannelCount,
			Report.ResultUVChannelCount,
			Report.bDestinationUVChannelExisted ? TEXT("Existing") : TEXT("New"),
			Report.bDestinationUVChannelContainedData ? TEXT("ContainsData") : TEXT("EmptyOrZero"),
			Report.bGeneratedMissingBasisForBake ? TEXT("Yes") : TEXT("No"),
			Report.DegenerateTriangleCount,
			Report.InvalidBasisCount,
			Report.InvalidNormalCount,
			Report.InvalidTangentCount,
			Report.InvalidBinormalSignCount,
			Report.InvalidDerivedBinormalCount,
			Report.UniqueEncodedNormalCount,
			Report.FallbackEncodedNormalCount,
			Report.MaximumHalfPrecisionAngularErrorDegrees,
			Report.MinimumCurvature,
			Report.MeanCurvature,
			Report.MaximumCurvature);
	}

	static void RestoreVertexInstanceBasis(
		const FMeshDescription& Source,
		FMeshDescription& Destination)
	{
		FStaticMeshConstAttributes SourceAttributes(Source);
		FStaticMeshAttributes DestinationAttributes(Destination);
		const TVertexInstanceAttributesConstRef<FVector3f> SourceNormals = SourceAttributes.GetVertexInstanceNormals();
		const TVertexInstanceAttributesConstRef<FVector3f> SourceTangents = SourceAttributes.GetVertexInstanceTangents();
		const TVertexInstanceAttributesConstRef<float> SourceSigns = SourceAttributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector3f> DestinationNormals = DestinationAttributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> DestinationTangents = DestinationAttributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> DestinationSigns = DestinationAttributes.GetVertexInstanceBinormalSigns();
		for (const FVertexInstanceID VertexInstanceID : Destination.VertexInstances().GetElementIDs())
		{
			DestinationNormals[VertexInstanceID] = SourceNormals[VertexInstanceID];
			DestinationTangents[VertexInstanceID] = SourceTangents[VertexInstanceID];
			DestinationSigns[VertexInstanceID] = SourceSigns[VertexInstanceID];
		}
	}

	static bool GenerateMissingStaticMeshBasis(
		UStaticMesh& Mesh,
		const int32 LODIndex,
		FMeshDescription& MeshDescription)
	{
		bool bHasInvalidNormals = false;
		bool bHasInvalidTangents = false;
		FStaticMeshOperations::HasInvalidVertexInstanceNormalsOrTangents(
			MeshDescription,
			bHasInvalidNormals,
			bHasInvalidTangents);
		if (!bHasInvalidNormals && !bHasInvalidTangents)
		{
			return false;
		}

		const FMeshBuildSettings& BuildSettings = Mesh.GetSourceModel(LODIndex).BuildSettings;
		const float ComparisonThreshold = BuildSettings.bRemoveDegenerates ? THRESH_POINTS_ARE_SAME : 0.0f;
		FStaticMeshOperations::ComputeTriangleTangentsAndNormals(
			MeshDescription,
			ComparisonThreshold,
			*Mesh.GetPathName());

		EComputeNTBsFlags Options = EComputeNTBsFlags::BlendOverlappingNormals;
		Options |= BuildSettings.bComputeWeightedNormals ? EComputeNTBsFlags::WeightedNTBs : EComputeNTBsFlags::None;
		Options |= BuildSettings.bUseMikkTSpace ? EComputeNTBsFlags::UseMikkTSpace : EComputeNTBsFlags::None;
		Options |= BuildSettings.bRemoveDegenerates ? EComputeNTBsFlags::IgnoreDegenerateTriangles : EComputeNTBsFlags::None;
		FStaticMeshOperations::ComputeTangentsAndNormals(MeshDescription, Options);
		return true;
	}

	static void BuildLODSelection(const int32 SourceModelCount, const UKenanSmoothNormalBakerSettings& Settings, TArray<int32>& OutLODIndices)
	{
		OutLODIndices.Reset();
		if (Settings.LODMode == EKenanSmoothNormalLODMode::SpecificLOD)
		{
			if (Settings.SpecificLOD >= 0 && Settings.SpecificLOD < SourceModelCount)
			{
				OutLODIndices.Add(Settings.SpecificLOD);
			}
			return;
		}

		for (int32 LODIndex = 0; LODIndex < SourceModelCount; ++LODIndex)
		{
			OutLODIndices.Add(LODIndex);
		}
	}

	template <typename TMeshType>
	static bool PrepareLODs(
		TMeshType& Mesh,
		const TArray<int32>& LODIndices,
		const FKenanSmoothNormalOptions& Options,
		TArray<FPreparedLOD>& OutPrepared,
		TArray<FString>& OutLines,
		FString& OutError)
	{
		OutPrepared.Reset();
		for (const int32 LODIndex : LODIndices)
		{
			FPreparedLOD& Prepared = OutPrepared.AddDefaulted_GetRef();
			Prepared.LODIndex = LODIndex;
			if (!Mesh.CloneMeshDescription(LODIndex, Prepared.MeshDescription))
			{
				OutLines.Add(FString::Printf(TEXT("LOD%d: 没有可编辑 MeshDescription，已跳过（通常是自动生成 LOD）。"), LODIndex));
				OutPrepared.Pop();
				continue;
			}

			FMeshDescription OriginalBasis;
			bool bGeneratedMissingBasis = false;
			if constexpr (std::is_same_v<TMeshType, UStaticMesh>)
			{
				OriginalBasis = Prepared.MeshDescription;
				bGeneratedMissingBasis = GenerateMissingStaticMeshBasis(Mesh, LODIndex, Prepared.MeshDescription);
			}

			if (!FKenanSmoothNormalCore::BakeMeshDescription(Prepared.MeshDescription, Options, Prepared.Report))
			{
				OutError = FString::Printf(TEXT("LOD%d 预计算失败：%s"), LODIndex, *Prepared.Report.Error);
				return false;
			}
			Prepared.Report.bGeneratedMissingBasisForBake = bGeneratedMissingBasis;
			if (bGeneratedMissingBasis)
			{
				RestoreVertexInstanceBasis(OriginalBasis, Prepared.MeshDescription);
			}
			OutLines.Add(FormatMeshReport(LODIndex, Prepared.Report));
		}

		if (OutPrepared.IsEmpty())
		{
			OutError = TEXT("选择范围内没有可编辑的 MeshDescription。");
			return false;
		}
		return true;
	}

	static UObject* DuplicateTarget(UObject* SourceAsset, const UKenanSmoothNormalBakerSettings& Settings, FString& OutError)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		const FString SourcePackageName = SourceAsset->GetOutermost()->GetName();
		FString DestinationPath = Settings.DuplicatePackagePath.TrimStartAndEnd();
		if (DestinationPath.IsEmpty())
		{
			DestinationPath = FPackageName::GetLongPackagePath(SourcePackageName);
		}
		if (!FPackageName::IsValidLongPackageName(DestinationPath))
		{
			OutError = FString::Printf(TEXT("副本包路径无效：%s"), *DestinationPath);
			return nullptr;
		}

		FString UniquePackageName;
		FString UniqueAssetName;
		const FString BasePackageName = DestinationPath + TEXT("/") + SourceAsset->GetName();
		AssetTools.CreateUniqueAssetName(BasePackageName, Settings.DuplicateSuffix, UniquePackageName, UniqueAssetName);
		const FString UniquePackagePath = FPackageName::GetLongPackagePath(UniquePackageName);
		UObject* Duplicated = AssetTools.DuplicateAsset(UniqueAssetName, UniquePackagePath, SourceAsset);
		if (!Duplicated)
		{
			OutError = FString::Printf(TEXT("无法生成副本：%s"), *UniquePackageName);
		}
		return Duplicated;
	}

	static bool SaveExactAsset(UObject* Asset, FString& OutError)
	{
		if (!Asset || !Asset->GetOutermost())
		{
			OutError = TEXT("输出资产或 Package 无效，无法保存。");
			return false;
		}
		TArray<UPackage*> PackagesToSave { Asset->GetOutermost() };
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);
		if (!bSaved || Asset->GetOutermost()->IsDirty())
		{
			OutError = FString::Printf(TEXT("定向保存失败：%s"), *Asset->GetPathName());
			return false;
		}
		return true;
	}

	static FKenanSmoothNormalOptions MakeCoreOptions(const UKenanSmoothNormalBakerSettings& Settings)
	{
		FKenanSmoothNormalOptions Options;
		Options.DestinationUVChannel = Settings.DestinationUVChannel;
		Options.WeldTolerance = Settings.WeldTolerance;
		Options.bComputeCurvature = Settings.bComputeCurvatureForReport;
		return Options;
	}

	static FString JoinReport(
		const UObject& Asset,
		const UKenanSmoothNormalBakerSettings& Settings,
		const TArray<FString>& Lines,
		const FString& Prefix)
	{
		TArray<FString> AllLines;
		AllLines.Add(Prefix);
		AllLines.Add(FString::Printf(TEXT("资产：%s"), *Asset.GetPathName()));
		AllLines.Add(FString::Printf(
			TEXT("引擎：%s"),
			*FEngineVersion::Current().ToString(EVersionComponent::Patch)));
		AllLines.Add(FString::Printf(TEXT("写入通道：UV%d；编码：Octahedral Float2；曲率：%s（不写入）"),
			Settings.DestinationUVChannel,
			Settings.bComputeCurvatureForReport ? TEXT("计算") : TEXT("跳过")));
		if (!Prefix.StartsWith(TEXT("PRECHECK")))
		{
			AllLines.Add(FString::Printf(TEXT("实际结果：%s"), *Asset.GetPathName()));
		}
		else if (Settings.OutputMode == EKenanSmoothNormalOutputMode::CreateDuplicate)
		{
			const FString DestinationPath = Settings.DuplicatePackagePath.TrimStartAndEnd().IsEmpty()
				? FPackageName::GetLongPackagePath(Asset.GetOutermost()->GetName())
				: Settings.DuplicatePackagePath.TrimStartAndEnd();
			AllLines.Add(FString::Printf(TEXT("输出：Create Duplicate -> %s/%s%s（冲突时自动唯一命名）"),
				*DestinationPath,
				*Asset.GetName(),
				*Settings.DuplicateSuffix));
		}
		else
		{
			AllLines.Add(TEXT("输出：Overwrite Source（执行前由界面二次确认）"));
		}
		AllLines.Append(Lines);
		return FString::Join(AllLines, TEXT("\n"));
	}

	static bool ValidateChannel(const UObject* Asset, const UKenanSmoothNormalBakerSettings& Settings, FString& OutError)
	{
		const int32 Maximum = FKenanSmoothNormalMeshAdapter::GetMaximumUVChannelCount(Asset);
		if (Maximum <= 0)
		{
			OutError = TEXT("不支持的资产类型。");
			return false;
		}
		if (Settings.DestinationUVChannel < 0 || Settings.DestinationUVChannel >= Maximum)
		{
			OutError = FString::Printf(
				TEXT("%s 仅允许 UV0–UV%d，当前选择 UV%d。"),
				Asset->IsA<USkeletalMesh>() ? TEXT("Skeletal Mesh") : TEXT("Static Mesh"),
				Maximum - 1,
				Settings.DestinationUVChannel);
			return false;
		}
		return true;
	}
}

bool FKenanSmoothNormalMeshAdapter::IsSupportedAsset(const UObject* Asset)
{
	return Asset && (Asset->IsA<UStaticMesh>() || Asset->IsA<USkeletalMesh>());
}

int32 FKenanSmoothNormalMeshAdapter::GetMaximumUVChannelCount(const UObject* Asset)
{
	if (Asset && Asset->IsA<UStaticMesh>())
	{
		return Kenan::SmoothNormalBaker::Editor::Private::StaticMeshMaxUVChannels;
	}
	if (Asset && Asset->IsA<USkeletalMesh>())
	{
		return Kenan::SmoothNormalBaker::Editor::Private::SkeletalMeshMaxUVChannels;
	}
	return 0;
}

FKenanAssetOperationReport FKenanSmoothNormalMeshAdapter::Preflight(
	UObject* SourceAsset,
	const UKenanSmoothNormalBakerSettings& Settings)
{
	using namespace Kenan::SmoothNormalBaker::Editor::Private;
	FKenanAssetOperationReport Result;
	if (!IsSupportedAsset(SourceAsset))
	{
		Result.Summary = TEXT("预检失败：只支持 Static Mesh 与 Skeletal Mesh。");
		return Result;
	}

	FString Error;
	if (!ValidateChannel(SourceAsset, Settings, Error))
	{
		Result.Summary = TEXT("预检失败：") + Error;
		return Result;
	}

	TArray<int32> LODIndices;
	TArray<FPreparedLOD> Prepared;
	TArray<FString> Lines;
	const FKenanSmoothNormalOptions Options = MakeCoreOptions(Settings);
	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(SourceAsset))
	{
		BuildLODSelection(StaticMesh->GetNumSourceModels(), Settings, LODIndices);
		if (!PrepareLODs(*StaticMesh, LODIndices, Options, Prepared, Lines, Error))
		{
			Result.Summary = TEXT("预检失败：") + Error;
			return Result;
		}
	}
	else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SourceAsset))
	{
		BuildLODSelection(SkeletalMesh->GetNumSourceModels(), Settings, LODIndices);
		if (!PrepareLODs(*SkeletalMesh, LODIndices, Options, Prepared, Lines, Error))
		{
			Result.Summary = TEXT("预检失败：") + Error;
			return Result;
		}
	}

	Result.bSucceeded = true;
	Result.ResultAsset = SourceAsset;
	Result.Summary = JoinReport(*SourceAsset, Settings, Lines, TEXT("PRECHECK PASS（未修改资产）"));
	return Result;
}

FKenanAssetOperationReport FKenanSmoothNormalMeshAdapter::Bake(
	UObject* SourceAsset,
	const UKenanSmoothNormalBakerSettings& Settings)
{
	using namespace Kenan::SmoothNormalBaker::Editor::Private;
	FKenanAssetOperationReport Result;
	if (!IsSupportedAsset(SourceAsset))
	{
		Result.Summary = TEXT("烘焙失败：只支持 Static Mesh 与 Skeletal Mesh。");
		return Result;
	}

	FString Error;
	if (!ValidateChannel(SourceAsset, Settings, Error))
	{
		Result.Summary = TEXT("烘焙失败：") + Error;
		return Result;
	}

	TArray<int32> LODIndices;
	TArray<FPreparedLOD> Prepared;
	TArray<FString> Lines;
	const FKenanSmoothNormalOptions Options = MakeCoreOptions(Settings);
	if (UStaticMesh* SourceStaticMesh = Cast<UStaticMesh>(SourceAsset))
	{
		BuildLODSelection(SourceStaticMesh->GetNumSourceModels(), Settings, LODIndices);
		if (!PrepareLODs(*SourceStaticMesh, LODIndices, Options, Prepared, Lines, Error))
		{
			Result.Summary = TEXT("烘焙失败（未创建或修改资产）：") + Error;
			return Result;
		}
	}
	else if (USkeletalMesh* SourceSkeletalMesh = Cast<USkeletalMesh>(SourceAsset))
	{
		BuildLODSelection(SourceSkeletalMesh->GetNumSourceModels(), Settings, LODIndices);
		if (!PrepareLODs(*SourceSkeletalMesh, LODIndices, Options, Prepared, Lines, Error))
		{
			Result.Summary = TEXT("烘焙失败（未创建或修改资产）：") + Error;
			return Result;
		}
	}

	UObject* TargetAsset = SourceAsset;
	if (Settings.OutputMode == EKenanSmoothNormalOutputMode::CreateDuplicate)
	{
		TargetAsset = DuplicateTarget(SourceAsset, Settings, Error);
		if (!TargetAsset)
		{
			Result.Summary = TEXT("烘焙失败：") + Error;
			return Result;
		}
	}

	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(TargetAsset))
	{

		const FScopedTransaction Transaction(LOCTEXT("BakeStaticMesh", "Kenan Tools: Bake Static Mesh Smooth Normals"));
		StaticMesh->Modify();
		for (FPreparedLOD& LOD : Prepared)
		{
			StaticMesh->CreateMeshDescription(LOD.LODIndex, MoveTemp(LOD.MeshDescription));
			StaticMesh->CommitMeshDescription(LOD.LODIndex);
		}
		StaticMesh->Build(false);
		StaticMesh->PostEditChange();
	}
	else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(TargetAsset))
	{
		const FScopedTransaction Transaction(LOCTEXT("BakeSkeletalMesh", "Kenan Tools: Bake Skeletal Mesh Smooth Normals"));
		SkeletalMesh->Modify();
		for (FPreparedLOD& LOD : Prepared)
		{
			SkeletalMesh->CreateMeshDescription(LOD.LODIndex, MoveTemp(LOD.MeshDescription));
			USkeletalMesh::FCommitMeshDescriptionParams CommitParams;
			Kenan::SmoothNormalBaker::Compat::ConfigureSkeletalMeshCommit(CommitParams);
			if (!SkeletalMesh->CommitMeshDescription(LOD.LODIndex, CommitParams))
			{
				Result.ResultAsset = TargetAsset;
				Result.Summary = FString::Printf(TEXT("烘焙部分失败：LOD%d CommitMeshDescription 返回 false；资产未自动保存。"), LOD.LODIndex);
				return Result;
			}
		}
		SkeletalMesh->Build();
		SkeletalMesh->PostEditChange();
	}

	Result.ResultAsset = TargetAsset;
	Result.bSucceeded = true;
	Result.Summary = JoinReport(*TargetAsset, Settings, Lines, TEXT("BAKE PASS"));
	if (Settings.bSaveOutputAsset)
	{
		if (!SaveExactAsset(TargetAsset, Error))
		{
			Result.bSucceeded = false;
			Result.Summary += TEXT("\nSAVE FAILED：") + Error + TEXT("\n计算已提交但保存未验证，必须人工处理该 dirty package。");
			return Result;
		}
		Result.bSaved = true;
		Result.Summary += TEXT("\nSAVE PASS：仅定向保存输出资产。");
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
