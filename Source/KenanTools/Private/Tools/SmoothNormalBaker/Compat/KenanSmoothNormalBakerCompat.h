#pragma once

#include "CoreMinimal.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/EngineVersionComparison.h"

namespace Kenan::SmoothNormalBaker::Compat
{
	inline void ConfigureSkeletalMeshCommit(USkeletalMesh::FCommitMeshDescriptionParams& Params)
	{
		Params.bMarkPackageDirty = true;
		Params.bForceUpdate = true;
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
		Params.bUpdateMorphTargets = true;
		Params.bUpdateSkinWeightProfiles = true;
		Params.bUpdateVertexAttributes = true;
		Params.bUpdateVertexColors = true;
#endif
	}
}
