#include "Tools/SmoothNormalBaker/Core/KenanSmoothNormalCore.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Math/Float16.h"
#include "Misc/AutomationTest.h"
#include "StaticMeshAttributes.h"

namespace Kenan::SmoothNormalBaker::Tests::Private
{
	static float AngularErrorDegrees(const FVector3f& A, const FVector3f& B)
	{
		const FVector3d NormalA(A);
		const FVector3d NormalB(B);
		const double Dot = FMath::Clamp(FVector3d::DotProduct(NormalA, NormalB), -1.0, 1.0);
		const double CrossLength = FVector3d::CrossProduct(NormalA, NormalB).Length();
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(CrossLength, Dot)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKenanOctahedralRoundTripTest,
	"KenanTools.SmoothNormalBaker.OctahedralRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKenanOctahedralRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace Kenan::SmoothNormalBaker::Tests::Private;

	TArray<FVector3f> Normals {
		FVector3f(1.0f, 0.0f, 0.0f),
		FVector3f(-1.0f, 0.0f, 0.0f),
		FVector3f(0.0f, 1.0f, 0.0f),
		FVector3f(0.0f, -1.0f, 0.0f),
		FVector3f(0.0f, 0.0f, 1.0f),
		FVector3f(0.0f, 0.0f, -1.0f)
	};
	FRandomStream Random(0x4B454E41);
	for (int32 Index = 0; Index < 4096; ++Index)
	{
		FVector3f Candidate(
			Random.FRandRange(-1.0f, 1.0f),
			Random.FRandRange(-1.0f, 1.0f),
			Random.FRandRange(-1.0f, 1.0f));
		if (!Candidate.IsNearlyZero())
		{
			Normals.Add(Candidate.GetSafeNormal());
		}
	}

	float MaximumFloatError = 0.0f;
	float MaximumHalfError = 0.0f;
	for (const FVector3f& Normal : Normals)
	{
		const FVector2f Encoded = FKenanSmoothNormalCore::EncodeOctahedral(Normal);
		const FVector3f Decoded = FKenanSmoothNormalCore::DecodeOctahedral(Encoded);
		const float FloatError = AngularErrorDegrees(Normal, Decoded);
		MaximumFloatError = FMath::Max(MaximumFloatError, FloatError);

		const FVector2f HalfEncoded(static_cast<float>(FFloat16(Encoded.X)), static_cast<float>(FFloat16(Encoded.Y)));
		const FVector3f HalfDecoded = FKenanSmoothNormalCore::DecodeOctahedral(HalfEncoded);
		const float HalfError = AngularErrorDegrees(Normal, HalfDecoded);
		MaximumHalfError = FMath::Max(MaximumHalfError, HalfError);
	}

	TestTrue(TEXT("float2 最大角误差 <= 0.01 度"), MaximumFloatError <= 0.01f);
	TestTrue(TEXT("half2 最大角误差 <= 0.25 度"), MaximumHalfError <= 0.25f);
	AddInfo(FString::Printf(TEXT("Samples=%d FloatMax=%.8f HalfMax=%.8f"), Normals.Num(), MaximumFloatError, MaximumHalfError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKenanOctahedralInvalidInputTest,
	"KenanTools.SmoothNormalBaker.OctahedralInvalidInputFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKenanOctahedralInvalidInputTest::RunTest(const FString& Parameters)
{
	const FVector2f EncodedZero = FKenanSmoothNormalCore::EncodeOctahedral(FVector3f::ZeroVector);
	const FVector3f DecodedZero = FKenanSmoothNormalCore::DecodeOctahedral(EncodedZero);
	TestTrue(TEXT("零向量回退编码有限"), FMath::IsFinite(EncodedZero.X) && FMath::IsFinite(EncodedZero.Y));
	TestTrue(TEXT("零向量回退解码为单位向量"), FMath::IsNearlyEqual(DecodedZero.Size(), 1.0f, 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKenanWeldedHardEdgeTest,
	"KenanTools.SmoothNormalBaker.WeldedHardEdgeContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKenanWeldedHardEdgeTest::RunTest(const FString& Parameters)
{
	FMeshDescription MeshDescription;
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);
	const FPolygonGroupID PolygonGroup = MeshDescription.CreatePolygonGroup();

	auto AddTriangle = [&](const TArray<FVector3f>& TrianglePositions, const FVector3f& Normal, TArray<FVertexInstanceID>& OutInstances)
	{
		OutInstances.Reset();
		for (const FVector3f& Position : TrianglePositions)
		{
			const FVertexID VertexID = MeshDescription.CreateVertex();
			Positions[VertexID] = Position;
			const FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(VertexID);
			Normals[InstanceID] = Normal;
			Tangents[InstanceID] = FVector3f::ForwardVector;
			Signs[InstanceID] = 1.0f;
			UVs.Set(InstanceID, 0, FVector2f::ZeroVector);
			OutInstances.Add(InstanceID);
		}
		MeshDescription.CreateTriangle(PolygonGroup, OutInstances);
	};

	TArray<FVertexInstanceID> XYInstances;
	TArray<FVertexInstanceID> XZInstances;
	AddTriangle(
		{ FVector3f(0, 0, 0), FVector3f(1, 0, 0), FVector3f(0, 1, 0) },
		FVector3f::UpVector,
		XYInstances);
	AddTriangle(
		{ FVector3f(0, 0, 0), FVector3f(0, 0, 1), FVector3f(1, 0, 0) },
		FVector3f(0, 1, 0),
		XZInstances);

	FKenanSmoothNormalOptions Options;
	Options.DestinationUVChannel = 3;
	Options.WeldTolerance = 0.0001f;
	Options.bComputeCurvature = true;
	FKenanSmoothNormalMeshReport Report;
	TestTrue(TEXT("BakeMeshDescription succeeds"), FKenanSmoothNormalCore::BakeMeshDescription(MeshDescription, Options, Report));

	const FVector3f XYTangentNormal = FKenanSmoothNormalCore::DecodeOctahedral(UVs.Get(XYInstances[0], 3));
	const FVector3f XZTangentNormal = FKenanSmoothNormalCore::DecodeOctahedral(UVs.Get(XZInstances[0], 3));
	const FVector3f XYLocal(
		XYTangentNormal.X,
		XYTangentNormal.Y,
		XYTangentNormal.Z);
	const FVector3f XZLocal(
		XZTangentNormal.X,
		XZTangentNormal.Z,
		-XZTangentNormal.Y);
	TestTrue(TEXT("分裂硬边还原到同一局部平滑方向"), FVector3f::DotProduct(XYLocal.GetSafeNormal(), XZLocal.GetSafeNormal()) > 0.9999f);
	TestEqual(TEXT("目标 UV 扩展到 4 通道"), Report.ResultUVChannelCount, 4);
	TestTrue(TEXT("曲率接口产出非零范围"), Report.MaximumCurvature > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FKenanReversedWindingFacingTest,
	"KenanTools.SmoothNormalBaker.ReversedWindingUsesSourceFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKenanReversedWindingFacingTest::RunTest(const FString& Parameters)
{
	FMeshDescription MeshDescription;
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);

	TArray<FVertexInstanceID> Instances;
	for (const FVector3f& Position : {
		FVector3f(0, 0, 0),
		FVector3f(0, 1, 0),
		FVector3f(1, 0, 0) })
	{
		const FVertexID VertexID = MeshDescription.CreateVertex();
		Positions[VertexID] = Position;
		const FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(VertexID);
		Normals[InstanceID] = FVector3f::UpVector;
		Tangents[InstanceID] = FVector3f::ForwardVector;
		Signs[InstanceID] = 1.0f;
		UVs.Set(InstanceID, 0, FVector2f::ZeroVector);
		Instances.Add(InstanceID);
	}
	MeshDescription.CreateTriangle(MeshDescription.CreatePolygonGroup(), Instances);

	// The geometric cross product for this ordering points down, while the source-facing
	// vertex normals point up. The baker must honor the source-facing orientation.
	FKenanSmoothNormalOptions Options;
	Options.DestinationUVChannel = 3;
	Options.WeldTolerance = 0.0001f;
	Options.bComputeCurvature = true;
	FKenanSmoothNormalMeshReport Report;
	TestTrue(TEXT("Reversed-winding bake succeeds"), FKenanSmoothNormalCore::BakeMeshDescription(MeshDescription, Options, Report));
	for (const FVertexInstanceID InstanceID : Instances)
	{
		const FVector3f Decoded = FKenanSmoothNormalCore::DecodeOctahedral(UVs.Get(InstanceID, 3));
		TestTrue(TEXT("Decoded normal remains source-facing"), FVector3f::DotProduct(Decoded, FVector3f::UpVector) > 0.9999f);
	}
	TestTrue(TEXT("Source-facing single triangle curvature stays near zero"), Report.MaximumCurvature < 0.001f);
	return true;
}

#endif
