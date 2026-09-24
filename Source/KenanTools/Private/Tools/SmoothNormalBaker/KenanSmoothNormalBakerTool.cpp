#include "Tools/SmoothNormalBaker/KenanSmoothNormalBakerTool.h"

#include "Tools/SmoothNormalBaker/Editor/SKenanSmoothNormalBakerPanel.h"

#define LOCTEXT_NAMESPACE "FKenanSmoothNormalBakerTool"

FName FKenanSmoothNormalBakerTool::GetToolId() const
{
	static const FName ToolId(TEXT("SmoothNormalBaker"));
	return ToolId;
}

FText FKenanSmoothNormalBakerTool::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "SmoothNormalBaker");
}

FText FKenanSmoothNormalBakerTool::GetDescription() const
{
	return LOCTEXT("Description", "将 Static／Skeletal Mesh 的平滑切线空间法线编码到可选 UV 通道。");
}

FName FKenanSmoothNormalBakerTool::GetCategory() const
{
	static const FName Category(TEXT("Mesh"));
	return Category;
}

int32 FKenanSmoothNormalBakerTool::GetSortOrder() const
{
	return 100;
}

TSharedPtr<SWidget> FKenanSmoothNormalBakerTool::CreateWidget()
{
	return SNew(SKenanSmoothNormalBakerPanel);
}

#undef LOCTEXT_NAMESPACE
