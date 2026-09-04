#include "RollbackArenaBuildContentCommandlet.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogRollbackArenaContent, Log, All);

namespace
{
constexpr TCHAR MapPackage[] = TEXT("/Game/Maps/RollbackArena");
constexpr TCHAR MaterialPackage[] = TEXT("/Game/Materials/M_RollbackTint");
constexpr TCHAR MaterialObject[] = TEXT("/Game/Materials/M_RollbackTint.M_RollbackTint");

bool ResolveOwnedOutput(const TCHAR* Package, const FString& Extension, FString& Filename)
{
    if (!FPackageName::TryConvertLongPackageNameToFilename(Package, Filename, Extension)) return false;
    Filename = FPaths::ConvertRelativePathToFull(Filename);
    FPaths::NormalizeFilename(Filename);
    FString ContentRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
    FPaths::NormalizeDirectoryName(ContentRoot);
    ContentRoot += TEXT("/");
    if (!Filename.StartsWith(ContentRoot, ESearchCase::IgnoreCase)) return false;
    return IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
}

bool ValidateMaterial(const UMaterial* Material)
{
    if (Material == nullptr || Material->GetBlendMode() != BLEND_Opaque ||
        !Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit) || Material->GetExpressions().Num() != 1) return false;
    const UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
    if (Data == nullptr) return false;
    const UMaterialExpressionVectorParameter* Tint = Cast<UMaterialExpressionVectorParameter>(Data->EmissiveColor.Expression);
    return Tint != nullptr && Tint->ParameterName == FName(TEXT("Tint")) &&
        Tint->DefaultValue.Equals(FLinearColor::White) && !Tint->bUseCustomPrimitiveData;
}

UMaterial* RebuildMaterial(const FString& Filename)
{
    const bool bExists = IFileManager::Get().FileExists(*Filename);
    UPackage* Package = bExists ? LoadPackage(nullptr, MaterialPackage, LOAD_None) : CreatePackage(MaterialPackage);
    if (Package == nullptr) return nullptr;
    Package->FullyLoad();
    UMaterial* Material = nullptr;
    if (bExists)
    {
        // Only the named generated material is owned; never replace a different
        // asset class merely because it occupies the expected output filename.
        Material = FindObject<UMaterial>(Package, TEXT("M_RollbackTint"));
        if (Material == nullptr) return nullptr;
    }
    else Material = NewObject<UMaterial>(Package, TEXT("M_RollbackTint"), RF_Public | RF_Standalone);
    if (Material == nullptr) return nullptr;
    Material->PreEditChange(nullptr);
    Material->MaterialDomain = MD_Surface;
    Material->BlendMode = BLEND_Opaque;
    Material->SetShadingModel(MSM_Unlit);
    Material->bUseMaterialAttributes = false;
    Material->TwoSided = false;

    // Regeneration replaces the owned graph rather than appending expressions.
    for (int32 Property = 0; Property < MP_MAX; ++Property)
        if (FExpressionInput* Input = Material->GetExpressionInputForProperty(static_cast<EMaterialProperty>(Property))) *Input = FExpressionInput();
    Material->GetExpressionCollection().Empty();
    UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
    Data->EmissiveColor = FColorMaterialInput();
    UMaterialExpressionVectorParameter* Tint = FindObject<UMaterialExpressionVectorParameter>(Material, TEXT("TintParameter"));
    if (Tint == nullptr) Tint = NewObject<UMaterialExpressionVectorParameter>(Material, TEXT("TintParameter"), RF_Transactional);
    Tint->Material = Material;
    Tint->ParameterName = FName(TEXT("Tint"));
    Tint->DefaultValue = FLinearColor::White;
    Tint->bUseCustomPrimitiveData = false;
    Tint->MaterialExpressionEditorX = -240;
    Tint->MaterialExpressionEditorY = 0;
    // Stable editor identities for this single generated node; canonical Core
    // state and hashes never contain any Unreal asset identity.
    Tint->ExpressionGUID = FGuid(0x524C5449, 0x4E540001, 0x00000000, 0x00000001);
    Tint->MaterialExpressionGuid = FGuid(0x524C5449, 0x4E540002, 0x00000000, 0x00000001);
    Material->GetExpressionCollection().AddExpression(Tint);
    Data->EmissiveColor.Connect(0, Tint);
    Material->PostEditChange();
    Material->MarkPackageDirty();
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager != nullptr) GShaderCompilingManager->FinishAllCompilation();
    if (!ValidateMaterial(Material)) return nullptr;
    if (!bExists) FAssetRegistryModule::AssetCreated(Material);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.bSlowTask = false;
    if (!UPackage::SavePackage(Package, Material, *Filename, SaveArgs)) return nullptr;
    return Material;
}
}

URollbackArenaBuildContentCommandlet::URollbackArenaBuildContentCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
    ShowErrorCount = true;
    HelpDescription = TEXT("Rebuild the native RollbackArena map and unlit Tint material.");
    HelpUsage = TEXT("-run=RollbackArenaBuildContent");
}

int32 URollbackArenaBuildContentCommandlet::Main(const FString&)
{
    if (GEditor == nullptr || !IsInGameThread())
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("Native content generation requires the Editor commandlet on its game thread."));
        return 1;
    }
    FString MaterialFilename, MapFilename;
    if (!ResolveOwnedOutput(MaterialPackage, FPackageName::GetAssetPackageExtension(), MaterialFilename) ||
        !ResolveOwnedOutput(MapPackage, FPackageName::GetMapPackageExtension(), MapFilename))
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("Generated asset paths could not be resolved inside project Content."));
        return 2;
    }
    if ((IFileManager::Get().FileExists(*MaterialFilename) && IFileManager::Get().IsReadOnly(*MaterialFilename)) ||
        (IFileManager::Get().FileExists(*MapFilename) && IFileManager::Get().IsReadOnly(*MapFilename)))
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("A generated asset is read-only; content was not overwritten."));
        return 3;
    }
    // Make an existing generated map the editor's current world, so NewBlankMap
    // tears it down before renaming/saving the replacement package at this path.
    if (IFileManager::Get().FileExists(*MapFilename) && UEditorLoadingAndSavingUtils::LoadMap(MapFilename) == nullptr)
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("The owned map path does not contain a loadable UWorld."));
        return 4;
    }
    TStrongObjectPtr<UMaterial> Material(RebuildMaterial(MaterialFilename));
    if (!Material.IsValid())
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("Could not build, validate or save the opaque unlit Tint material."));
        return 5;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (World == nullptr || World->PersistentLevel == nullptr || World->GetWorldSettings() == nullptr)
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("Could not create an empty Editor world."));
        return 6;
    }
    World->GetWorldSettings()->DefaultGameMode = nullptr;
    World->MarkPackageDirty();
    if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPackage))
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("Could not save the generated arena map."));
        return 7;
    }
    UPackage::WaitForAsyncFileWrites();
    if (IFileManager::Get().FileSize(*MaterialFilename) <= 0 || IFileManager::Get().FileSize(*MapFilename) <= 0 ||
        !ValidateMaterial(Material.Get()) || World->GetWorldSettings()->DefaultGameMode != nullptr)
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("Saved content failed material/map validation."));
        return 8;
    }
    // Reopen the map through the Editor loader; the independent Content tests
    // also load both saved packages in the next fresh process.
    UWorld* Reloaded = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
    if (Reloaded == nullptr || Reloaded->PersistentLevel == nullptr || Reloaded->GetWorldSettings() == nullptr ||
        Reloaded->GetWorldSettings()->DefaultGameMode != nullptr ||
        !ValidateMaterial(LoadObject<UMaterial>(nullptr, MaterialObject)))
    {
        UE_LOG(LogRollbackArenaContent, Error, TEXT("Saved arena content could not be reopened with the expected contracts."));
        return 9;
    }
    UE_LOG(LogRollbackArenaContent, Display, TEXT("Generated and validated %s and %s; Tint -> EmissiveColor, opaque unlit, map inherits project GameMode."), MaterialPackage, MapPackage);
    return 0;
}
