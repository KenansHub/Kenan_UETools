#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FKenanSmoothNormalBakerTool;
class SDockTab;
class FSpawnTabArgs;

DECLARE_LOG_CATEGORY_EXTERN(LogKenanTools, Log, All);

class FKenanToolsModule final : public IModuleInterface
{
public:
	virtual ~FKenanToolsModule() override;
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenPluginTab();
	TSharedRef<SDockTab> SpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);

	TUniquePtr<FKenanSmoothNormalBakerTool> SmoothNormalBakerTool;
};
