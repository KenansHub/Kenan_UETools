#include "Tools/SmoothNormalBaker/Core/KenanSmoothNormalCore.h"

#include "Math/Float16.h"
#include "StaticMeshAttributes.h"

namespace Kenan::SmoothNormal::Private
{
	struct FWeldGroup
	{
		FVector3f Representative = FVector3f::ZeroVector;
		FVector3f WeightedNormal = FVector3f::ZeroVector;
		TArray<FVertexInstanceID> VertexInstances;
	};

	struct FWeldData
	{
		TArray<FWeldGroup> Groups;
		TMap<FVertexID, int32> VertexToGroup;
		TMap<FVertexInstanceID, int32> VertexInstanceToGroup;
	};

	static float SignNotZero(const float Value)
	{
		return Value >= 0.0f ? 1.0f : -1.0f;
	}

	static FIntVector CellForPosition(const FVector3f& Position, const float CellSize)
	{
		return FIntVector(
			FMath::FloorToInt(Position.X / CellSize),
			FMath::FloorToInt(Position.Y / CellSize),
			FMath::FloorToInt(Position.Z / CellSize));
	}

	static bool BuildWeldData(
		const FMeshDescription& MeshDescription,
		const float WeldTolerance,
		FWeldData& OutData,
		FString& OutError)
	{
		if (!FMath::IsFinite(WeldTolerance) || WeldTolerance <= 0.0f)
		{
			OutError = TEXT("WeldTolerance 必须是有限正数。");
			return false;
		}

		FStaticMeshConstAttributes Attributes(MeshDescription);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		if (!Positions.IsValid())
		{
			OutError = TEXT("MeshDescription 缺少顶点位置属性。");
			return false;
		}

		TMap<FIntVector, TArray<int32>> CellToGroups;
		const float ToleranceSquared = WeldTolerance * WeldTolerance;

		for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[VertexID];
			if (Position.ContainsNaN())
			{
				OutError = FString::Printf(TEXT("顶点 %d 的位置不是有限值。"), VertexID.GetValue());
				return false;
			}

			const FIntVector Cell = CellForPosition(Position, WeldTolerance);
			int32 GroupIndex = INDEX_NONE;
			for (int32 Z = -1; Z <= 1 && GroupIndex == INDEX_NONE; ++Z)
			{
				for (int32 Y = -1; Y <= 1 && GroupIndex == INDEX_NONE; ++Y)
				{
					for (int32 X = -1; X <= 1 && GroupIndex == INDEX_NONE; ++X)
					{
						const FIntVector Neighbor = Cell + FIntVector(X, Y, Z);
						if (const TArray<int32>* Candidates = CellToGroups.Find(Neighbor))
						{
							for (const int32 Candidate : *Candidates)
							{
								if (FVector3f::DistSquared(Position, OutData.Groups[Candidate].Representative) <= ToleranceSquared)
								{
									GroupIndex = Candidate;
									break;
								}
							}
						}
					}
				}
			}

			if (GroupIndex == INDEX_NONE)
			{
				GroupIndex = OutData.Groups.AddDefaulted();
				OutData.Groups[GroupIndex].Representative = Position;
				CellToGroups.FindOrAdd(Cell).Add(GroupIndex);
			}
			OutData.VertexToGroup.Add(VertexID, GroupIndex);
		}

		for (const FVertexInstanceID VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
		{
			const FVertexID VertexID = MeshDescription.GetVertexInstanceVertex(VertexInstanceID);
			const int32* GroupIndex = OutData.VertexToGroup.Find(VertexID);
			if (!GroupIndex)
			{
				OutError = TEXT("VertexInstance 无法映射到焊接组。");
				return false;
			}
			OutData.VertexInstanceToGroup.Add(VertexInstanceID, *GroupIndex);
			OutData.Groups[*GroupIndex].VertexInstances.Add(VertexInstanceID);
		}

		return true;
	}

	static int32 AccumulateAreaWeightedNormals(const FMeshDescription& MeshDescription, FWeldData& WeldData)
	{
		FStaticMeshConstAttributes Attributes(MeshDescription);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		const TVertexInstanceAttributesConstRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
		int32 DegenerateCount = 0;

		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexID> Vertices = MeshDescription.GetTriangleVertices(TriangleID);
			if (Vertices.Num() != 3)
			{
				++DegenerateCount;
				continue;
			}

			const FVector3f A = Positions[Vertices[0]];
			const FVector3f B = Positions[Vertices[1]];
			const FVector3f C = Positions[Vertices[2]];
			FVector3f WeightedNormal = FVector3f::CrossProduct(B - A, C - A);
			if (WeightedNormal.ContainsNaN() || WeightedNormal.SizeSquared() <= UE_SMALL_NUMBER)
			{
				++DegenerateCount;
				continue;
			}

			// MeshDescription triangle winding is not a portable outward-facing contract across
			// importers. Preserve the area magnitude, but orient it to the actual source-facing
			// vertex normals so an inverted winding cannot turn an outline bake inward.
			FVector3f SourceFacing = FVector3f::ZeroVector;
			for (const FVertexInstanceID VertexInstanceID : MeshDescription.GetTriangleVertexInstances(TriangleID))
			{
				const FVector3f SourceNormal = InstanceNormals[VertexInstanceID];
				if (!SourceNormal.ContainsNaN() && SourceNormal.SizeSquared() > UE_SMALL_NUMBER)
				{
					SourceFacing += SourceNormal.GetSafeNormal();
				}
			}
			if (!SourceFacing.IsNearlyZero()
				&& FVector3f::DotProduct(WeightedNormal, SourceFacing) < 0.0f)
			{
				WeightedNormal *= -1.0f;
			}

			for (const FVertexID VertexID : Vertices)
			{
				if (const int32* GroupIndex = WeldData.VertexToGroup.Find(VertexID))
				{
					WeldData.Groups[*GroupIndex].WeightedNormal += WeightedNormal;
				}
			}
		}

		return DegenerateCount;
	}

	static FVector3f SafeGroupNormal(const FWeldGroup& Group, const FVector3f& Fallback)
	{
		if (!Group.WeightedNormal.ContainsNaN() && Group.WeightedNormal.SizeSquared() > UE_SMALL_NUMBER)
		{
			return Group.WeightedNormal.GetSafeNormal();
		}
		return Fallback.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
	}
}

FVector2f FKenanSmoothNormalCore::EncodeOctahedral(const FVector3f& UnitNormal)
{
	using namespace Kenan::SmoothNormal::Private;
	FVector3f Normal = UnitNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
	const float Denominator = FMath::Abs(Normal.X) + FMath::Abs(Normal.Y) + FMath::Abs(Normal.Z);
	Normal /= FMath::Max(Denominator, UE_SMALL_NUMBER);

	FVector2f Encoded(Normal.X, Normal.Y);
	if (Normal.Z < 0.0f)
	{
		Encoded = FVector2f(
			(1.0f - FMath::Abs(Encoded.Y)) * SignNotZero(Encoded.X),
			(1.0f - FMath::Abs(Encoded.X)) * SignNotZero(Encoded.Y));
	}
	return Encoded * 0.5f + FVector2f(0.5f, 0.5f);
}

FVector3f FKenanSmoothNormalCore::DecodeOctahedral(const FVector2f& EncodedNormal)
{
	using namespace Kenan::SmoothNormal::Private;
	const FVector2f Oct = EncodedNormal * 2.0f - FVector2f(1.0f, 1.0f);
	FVector3f Normal(Oct.X, Oct.Y, 1.0f - FMath::Abs(Oct.X) - FMath::Abs(Oct.Y));
	if (Normal.Z < 0.0f)
	{
		const float OldX = Normal.X;
		Normal.X = (1.0f - FMath::Abs(Normal.Y)) * SignNotZero(OldX);
		Normal.Y = (1.0f - FMath::Abs(OldX)) * SignNotZero(Normal.Y);
	}
	return Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
}

bool FKenanSmoothNormalCore::BakeMeshDescription(
	FMeshDescription& MeshDescription,
	const FKenanSmoothNormalOptions& Options,
	FKenanSmoothNormalMeshReport& OutReport)
{
	using namespace Kenan::SmoothNormal::Private;
	OutReport = FKenanSmoothNormalMeshReport();
	OutReport.VertexCount = MeshDescription.Vertices().Num();
	OutReport.VertexInstanceCount = MeshDescription.VertexInstances().Num();
	OutReport.TriangleCount = MeshDescription.Triangles().Num();

	if (Options.DestinationUVChannel < 0)
	{
		OutReport.Error = TEXT("写入 UV 通道不能为负数。");
		return false;
	}

	FStaticMeshAttributes Attributes(MeshDescription);
	TVertexInstanceAttributesRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> InstanceTangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	if (!InstanceNormals.IsValid() || !InstanceTangents.IsValid() || !BinormalSigns.IsValid() || !UVs.IsValid())
	{
		OutReport.Error = TEXT("MeshDescription 缺少法线、切线、副切线符号或 UV 属性。");
		return false;
	}

	FWeldData WeldData;
	if (!BuildWeldData(MeshDescription, Options.WeldTolerance, WeldData, OutReport.Error))
	{
		return false;
	}
	OutReport.WeldedGroupCount = WeldData.Groups.Num();
	OutReport.DegenerateTriangleCount = AccumulateAreaWeightedNormals(MeshDescription, WeldData);
	OutReport.PreviousUVChannelCount = UVs.GetNumChannels();
	OutReport.bDestinationUVChannelExisted = Options.DestinationUVChannel < UVs.GetNumChannels();
	if (OutReport.bDestinationUVChannelExisted)
	{
		for (const FVertexInstanceID VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
		{
			const FVector2f ExistingValue = UVs.Get(VertexInstanceID, Options.DestinationUVChannel);
			if (!ExistingValue.IsNearlyZero())
			{
				OutReport.bDestinationUVChannelContainedData = true;
				break;
			}
		}
	}
	if (UVs.GetNumChannels() <= Options.DestinationUVChannel)
	{
		UVs.SetNumChannels(Options.DestinationUVChannel + 1);
	}
	OutReport.ResultUVChannelCount = UVs.GetNumChannels();

	TMap<FVertexInstanceID, float> Curvature;
	if (Options.bComputeCurvature)
	{
		FString CurvatureError;
		if (!ComputeCurvature(MeshDescription, Options.WeldTolerance, Curvature, CurvatureError))
		{
			OutReport.Error = CurvatureError;
			return false;
		}
	}

	double CurvatureSum = 0.0;
	OutReport.MinimumCurvature = Curvature.Num() > 0 ? TNumericLimits<float>::Max() : 0.0f;
	TSet<FIntPoint> UniqueEncodedNormals;
	for (const FVertexInstanceID VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
	{
		const int32* GroupIndex = WeldData.VertexInstanceToGroup.Find(VertexInstanceID);
		if (!GroupIndex)
		{
			OutReport.Error = TEXT("烘焙阶段丢失 VertexInstance 焊接组映射。");
			return false;
		}

		if (const float* Value = Curvature.Find(VertexInstanceID))
		{
			OutReport.MinimumCurvature = FMath::Min(OutReport.MinimumCurvature, *Value);
			OutReport.MaximumCurvature = FMath::Max(OutReport.MaximumCurvature, *Value);
			CurvatureSum += *Value;
		}

		const FVector3f RawNormal = InstanceNormals[VertexInstanceID];
		const FVector3f RawTangent = InstanceTangents[VertexInstanceID];
		const float RawBinormalSign = BinormalSigns[VertexInstanceID];
		const bool bNormalValid = !RawNormal.ContainsNaN() && RawNormal.SizeSquared() > UE_SMALL_NUMBER;
		const bool bTangentValid = !RawTangent.ContainsNaN() && RawTangent.SizeSquared() > UE_SMALL_NUMBER;
		const bool bBinormalSignValid = FMath::IsFinite(RawBinormalSign) && FMath::Abs(RawBinormalSign) > UE_SMALL_NUMBER;
		OutReport.InvalidNormalCount += bNormalValid ? 0 : 1;
		OutReport.InvalidTangentCount += bTangentValid ? 0 : 1;
		OutReport.InvalidBinormalSignCount += bBinormalSignValid ? 0 : 1;

		const FVector3f NormalZ = RawNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		const FVector3f TangentX = RawTangent.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::ForwardVector);
		const FVector3f RawTangentY = FVector3f::CrossProduct(NormalZ, TangentX);
		const bool bDerivedBinormalValid = !RawTangentY.ContainsNaN() && RawTangentY.SizeSquared() > UE_SMALL_NUMBER;
		OutReport.InvalidDerivedBinormalCount += bDerivedBinormalValid ? 0 : 1;
		const FVector3f TangentY = RawTangentY.GetSafeNormal() * RawBinormalSign;
		if (!bNormalValid || !bTangentValid || !bBinormalSignValid || !bDerivedBinormalValid || TangentY.ContainsNaN() || TangentY.IsNearlyZero())
		{
			++OutReport.InvalidBasisCount;
			const FVector2f FallbackEncoded = EncodeOctahedral(FVector3f::UpVector);
			UVs.Set(VertexInstanceID, Options.DestinationUVChannel, FallbackEncoded);
			UniqueEncodedNormals.Add(FIntPoint(
				FMath::RoundToInt(FallbackEncoded.X * 1000000.0f),
				FMath::RoundToInt(FallbackEncoded.Y * 1000000.0f)));
			++OutReport.FallbackEncodedNormalCount;
			continue;
		}

		const FVector3f SmoothLocal = SafeGroupNormal(WeldData.Groups[*GroupIndex], NormalZ);
		const FVector3f SmoothTangent(
			FVector3f::DotProduct(SmoothLocal, TangentX),
			FVector3f::DotProduct(SmoothLocal, TangentY),
			FVector3f::DotProduct(SmoothLocal, NormalZ));
		const FVector3f UnitSmoothTangent = SmoothTangent.GetSafeNormal(UE_SMALL_NUMBER, FVector3f::UpVector);
		const FVector2f Encoded = EncodeOctahedral(UnitSmoothTangent);
		UVs.Set(VertexInstanceID, Options.DestinationUVChannel, Encoded);
		UniqueEncodedNormals.Add(FIntPoint(
			FMath::RoundToInt(Encoded.X * 1000000.0f),
			FMath::RoundToInt(Encoded.Y * 1000000.0f)));
		const FVector2f HalfEncoded(static_cast<float>(FFloat16(Encoded.X)), static_cast<float>(FFloat16(Encoded.Y)));
		const FVector3f HalfDecoded = DecodeOctahedral(HalfEncoded);
		const float HalfError = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
			FVector3f::DotProduct(UnitSmoothTangent, HalfDecoded), -1.0f, 1.0f)));
		OutReport.MaximumHalfPrecisionAngularErrorDegrees = FMath::Max(
			OutReport.MaximumHalfPrecisionAngularErrorDegrees,
			HalfError);

	}
	OutReport.UniqueEncodedNormalCount = UniqueEncodedNormals.Num();

	if (Curvature.Num() > 0)
	{
		OutReport.MeanCurvature = static_cast<float>(CurvatureSum / static_cast<double>(Curvature.Num()));
	}
	if (OutReport.InvalidBasisCount > 0)
	{
		OutReport.Error = FString::Printf(
			TEXT("%d/%d 个 VertexInstance 缺少有效切线基，拒绝写入回退法线。"),
			OutReport.InvalidBasisCount,
			OutReport.VertexInstanceCount);
		return false;
	}
	return true;
}

bool FKenanSmoothNormalCore::ComputeCurvature(
	const FMeshDescription& MeshDescription,
	const float WeldTolerance,
	TMap<FVertexInstanceID, float>& OutCurvature,
	FString& OutError)
{
	using namespace Kenan::SmoothNormal::Private;
	OutCurvature.Reset();

	FStaticMeshConstAttributes Attributes(MeshDescription);
	const TVertexInstanceAttributesConstRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
	if (!InstanceNormals.IsValid())
	{
		OutError = TEXT("曲率计算需要 VertexInstance Normal。");
		return false;
	}

	FWeldData WeldData;
	if (!BuildWeldData(MeshDescription, WeldTolerance, WeldData, OutError))
	{
		return false;
	}
	AccumulateAreaWeightedNormals(MeshDescription, WeldData);

	for (const FWeldGroup& Group : WeldData.Groups)
	{
		double Sum = 0.0;
		int32 Count = 0;
		const FVector3f Smooth = SafeGroupNormal(Group, FVector3f::UpVector);
		for (const FVertexInstanceID VertexInstanceID : Group.VertexInstances)
		{
			const FVector3f Source = InstanceNormals[VertexInstanceID].GetSafeNormal(UE_SMALL_NUMBER, Smooth);
			Sum += FMath::Max(0.0f, 1.0f - FVector3f::DotProduct(Source, Smooth));
			++Count;
		}
		const float Curvature = Count > 0 ? FMath::Clamp(FMath::Sqrt(static_cast<float>(Sum / Count)), 0.0f, 1.0f) : 0.0f;
		for (const FVertexInstanceID VertexInstanceID : Group.VertexInstances)
		{
			OutCurvature.Add(VertexInstanceID, Curvature);
		}
	}
	return true;
}
