#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

struct FKenanSmoothNormalOptions
{
	int32 DestinationUVChannel = 3;
	float WeldTolerance = 0.0001f;
	bool bComputeCurvature = true;
};

struct FKenanSmoothNormalMeshReport
{
	int32 VertexCount = 0;
	int32 VertexInstanceCount = 0;
	int32 TriangleCount = 0;
	int32 WeldedGroupCount = 0;
	int32 DegenerateTriangleCount = 0;
	int32 InvalidBasisCount = 0;
	int32 InvalidNormalCount = 0;
	int32 InvalidTangentCount = 0;
	int32 InvalidBinormalSignCount = 0;
	int32 InvalidDerivedBinormalCount = 0;
	int32 UniqueEncodedNormalCount = 0;
	int32 FallbackEncodedNormalCount = 0;
	bool bGeneratedMissingBasisForBake = false;
	int32 PreviousUVChannelCount = 0;
	int32 ResultUVChannelCount = 0;
	bool bDestinationUVChannelExisted = false;
	bool bDestinationUVChannelContainedData = false;
	float MinimumCurvature = 0.0f;
	float MaximumCurvature = 0.0f;
	float MeanCurvature = 0.0f;
	float MaximumHalfPrecisionAngularErrorDegrees = 0.0f;
	FString Error;
};

class FKenanSmoothNormalCore
{
public:
	static FVector2f EncodeOctahedral(const FVector3f& UnitNormal);
	static FVector3f DecodeOctahedral(const FVector2f& EncodedNormal);

	/** Computes and writes only the selected UV channel of the supplied MeshDescription. */
	static bool BakeMeshDescription(
		FMeshDescription& MeshDescription,
		const FKenanSmoothNormalOptions& Options,
		FKenanSmoothNormalMeshReport& OutReport);

	/** Curvature remains an independent result so storage can be added later without changing normal encoding. */
	static bool ComputeCurvature(
		const FMeshDescription& MeshDescription,
		float WeldTolerance,
		TMap<FVertexInstanceID, float>& OutCurvature,
		FString& OutError);
};
