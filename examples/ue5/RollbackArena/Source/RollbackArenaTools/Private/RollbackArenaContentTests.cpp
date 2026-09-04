#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRollbackArenaMapContent, "RollbackLab.Content.ArenaMap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRollbackArenaMapContent::RunTest(const FString&)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(TEXT("/Game/Maps/RollbackArena"), FPackageName::GetMapPackageExtension());
    if (!TestTrue(TEXT("Generated arena map is saved to project Content"), IFileManager::Get().FileSize(*Filename) > 0)) return false;
    UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/RollbackArena.RollbackArena"));
    if (!TestNotNull(TEXT("Saved arena map loads as a UWorld"), World)) return false;
    TestNotNull(TEXT("Saved map has a persistent level"), World->PersistentLevel.Get());
    AWorldSettings* Settings = World->GetWorldSettings();
    if (!TestNotNull(TEXT("Saved map has WorldSettings"), Settings)) return false;
    TestTrue(TEXT("Map inherits project default GameMode"), Settings->DefaultGameMode == nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRollbackArenaTintContent, "RollbackLab.Content.UnlitTintMaterial", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRollbackArenaTintContent::RunTest(const FString&)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(TEXT("/Game/Materials/M_RollbackTint"), FPackageName::GetAssetPackageExtension());
    if (!TestTrue(TEXT("Generated Tint material is saved to project Content"), IFileManager::Get().FileSize(*Filename) > 0)) return false;
    UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_RollbackTint.M_RollbackTint"));
    if (!TestNotNull(TEXT("Saved material loads"), Material)) return false;
    TestEqual(TEXT("Material is opaque"), static_cast<int32>(Material->GetBlendMode()), static_cast<int32>(BLEND_Opaque));
    TestTrue(TEXT("Material is only unlit"), Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
    TestEqual(TEXT("Rebuilt material has one expression"), Material->GetExpressions().Num(), 1);
    const UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
    if (!TestNotNull(TEXT("Editor graph is retained in asset"), Data)) return false;
    const UMaterialExpressionVectorParameter* Tint = Cast<UMaterialExpressionVectorParameter>(Data->EmissiveColor.Expression);
    if (!TestNotNull(TEXT("Tint vector is directly connected to EmissiveColor"), Tint)) return false;
    TestEqual(TEXT("Runtime parameter name is Tint"), Tint->ParameterName, FName(TEXT("Tint")));
    TestTrue(TEXT("Tint defaults to white"), Tint->DefaultValue.Equals(FLinearColor::White));
    TestFalse(TEXT("Tint uses instance parameters rather than custom primitive data"), Tint->bUseCustomPrimitiveData);
    return true;
}
#endif
