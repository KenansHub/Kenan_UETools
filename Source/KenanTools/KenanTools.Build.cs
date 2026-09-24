using UnrealBuildTool;

public class KenanTools : ModuleRules
{
    public KenanTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "MeshDescription",
            "StaticMeshDescription"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetTools",
            "ContentBrowser",
            "InputCore",
            "LevelEditor",
            "Projects",
            "PropertyEditor",
            "SkeletalMeshDescription",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
