#pragma once

#include "IKenanEditorTool.h"

class FKenanSmoothNormalBakerTool final : public IKenanEditorTool
{
public:
	virtual FName GetToolId() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FName GetCategory() const override;
	virtual int32 GetSortOrder() const override;
	virtual TSharedPtr<SWidget> CreateWidget() override;
};
