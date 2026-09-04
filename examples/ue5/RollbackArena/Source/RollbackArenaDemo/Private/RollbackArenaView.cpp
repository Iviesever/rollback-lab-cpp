#include "RollbackArenaView.h"
#include "RollbackArenaHUD.h"
#include "RollbackLabSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "InputCoreTypes.h"
#include "UnrealClient.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
constexpr double ViewScale=0.45;
constexpr double PeerOffset=260.0;
bool ReadUnsigned(const TCHAR* Key, uint64& Value, FString& Error)
{
    FString Text;
    const FString Prefix=FString(Key)+TEXT("=");
    if(!FParse::Value(FCommandLine::Get(),*Prefix,Text,false))
    {
        if(FString(FCommandLine::Get()).Contains(Prefix)) { Error=TEXT("Empty numeric option: ")+FString(Key); return false; }
        return true;
    }
    if(Text.IsEmpty()) { Error=TEXT("Empty numeric option: ")+FString(Key); return false; }
    uint64 Parsed=0;
    for(TCHAR Character:Text)
    {
        if(Character<TEXT('0') || Character>TEXT('9')) { Error=TEXT("Expected unsigned decimal: ")+FString(Key);return false; }
        const uint64 Digit=static_cast<uint64>(Character-TEXT('0'));
        if(Parsed>(MAX_uint64-Digit)/10U) { Error=TEXT("Numeric option overflow: ")+FString(Key);return false; }
        Parsed=Parsed*10U+Digit;
    }
    Value=Parsed; return true;
}
FString Quoted(const FString& Value)
{
    FString Result=TEXT("\"");
    for(TCHAR C:Value)
    {
        if(C==TEXT('"')) Result+=TEXT("\\\"");
        else if(C==TEXT('\\')) Result+=TEXT("\\\\");
        else if(C==TEXT('\n')) Result+=TEXT("\\n");
        else if(C==TEXT('\r')) Result+=TEXT("\\r");
        else if(C==TEXT('\t')) Result+=TEXT("\\t");
        else if(C<32) Result+=FString::Printf(TEXT("\\u%04x"),static_cast<uint32>(C));
        else Result.AppendChar(C);
    }
    return Result+TEXT("\"");
}
FString Hex(uint64 Value) { return FString::Printf(TEXT("0x%016llX"),Value); }
}

ARollbackArenaView::ARollbackArenaView()
{
    PrimaryActorTick.bCanEverTick=true;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("PresentationRoot"));
    SetRootComponent(SceneRoot);
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("OverviewCamera"));
    Camera->SetupAttachment(SceneRoot);
    Camera->ProjectionMode=ECameraProjectionMode::Orthographic;
    Camera->OrthoWidth=1120.0f;
    Camera->AspectRatio=16.0f/9.0f;
    Camera->bConstrainAspectRatio=true;
    Camera->SetRelativeLocation(FVector(0,0,1000));
    Camera->SetRelativeRotation(FRotator(-90,-90,0));
    auto Component=[this](const FString& Name)
    {
        UStaticMeshComponent* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*Name);
        Mesh->SetupAttachment(SceneRoot);
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
        Mesh->SetVisibility(false);
        return Mesh;
    };
    for(uint32 Peer=0;Peer<2;++Peer)
    {
        Floors.Add(Component(FString::Printf(TEXT("Floor%u"),Peer)));
        for(uint32 Player=0;Player<2;++Player) Players.Add(Component(FString::Printf(TEXT("Peer%uPlayer%u"),Peer,Player)));
        for(uint32 Slot=0;Slot<64;++Slot) Projectiles.Add(Component(FString::Printf(TEXT("Peer%uProjectile%u"),Peer,Slot)));
    }
}
ARollbackArenaView::~ARollbackArenaView()=default;

FVector ARollbackArenaView::ProjectPosition(uint32 Peer,int32 X,int32 Y,double Z)
{
    return FVector((Peer==0?-PeerOffset:PeerOffset)+(static_cast<double>(X)/1024.0-512.0)*ViewScale,
                   (288.0-static_cast<double>(Y)/1024.0)*ViewScale,Z);
}

bool ARollbackArenaView::InitializeVisuals()
{
    UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UMaterialInterface* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Materials/M_RollbackTint.M_RollbackTint"));
    if(!Cube||!Cylinder||!Sphere||!Material) { Failure=TEXT("Arena content missing. Run GenerateUnrealContent before launch/cook.");return false; }
    const FLinearColor Tints[]={FLinearColor(0.02f,0.035f,0.055f),FLinearColor(0.018f,0.032f,0.052f),
        FLinearColor(0.04f,0.85f,0.9f),FLinearColor(1.0f,0.20f,0.28f),
        FLinearColor(0.25f,1.0f,1.0f),FLinearColor(1.0f,0.5f,0.2f)};
    for(const FLinearColor& Tint:Tints)
    {
        UMaterialInstanceDynamic* Instance=UMaterialInstanceDynamic::Create(Material,this);
        Instance->SetVectorParameterValue(TEXT("Tint"),Tint);
        Materials.Add(Instance);
    }
    for(uint32 Peer=0;Peer<2;++Peer)
    {
        Floors[Peer]->SetStaticMesh(Cube); Floors[Peer]->SetMaterial(0,Materials[Peer]);
        Floors[Peer]->SetRelativeLocation(FVector(Peer==0?-PeerOffset:PeerOffset,0,-5));
        Floors[Peer]->SetRelativeScale3D(FVector(1024.0*ViewScale/100.0,576.0*ViewScale/100.0,0.08));
        Floors[Peer]->SetVisibility(true);
        for(uint32 Player=0;Player<2;++Player)
        {
            UStaticMeshComponent* Mesh=Players[Peer*2+Player];
            Mesh->SetStaticMesh(Cylinder); Mesh->SetMaterial(0,Materials[2+Player]);
            Mesh->SetRelativeScale3D(FVector(0.108,0.108,0.10));
        }
        for(uint32 Slot=0;Slot<64;++Slot)
        {
            UStaticMeshComponent* Mesh=Projectiles[Peer*64+Slot];
            Mesh->SetStaticMesh(Sphere);Mesh->SetRelativeScale3D(FVector(0.05));
        }
    }
    return true;
}

bool ARollbackArenaView::ReadSettings(RollbackArena::FSettings& Settings)
{
    bSmoke=FParse::Param(FCommandLine::Get(),TEXT("RollbackLabSmoke"));
    // Establish failure evidence destinations before validating any other option.
    FParse::Value(FCommandLine::Get(),TEXT("RollbackLabTrace="),TracePath,false);
    FParse::Value(FCommandLine::Get(),TEXT("RollbackLabCaptureDir="),CaptureDirectory,false);
    FParse::Value(FCommandLine::Get(),TEXT("RollbackLabGitSha="),RequestedGitSha,false);
    if(bSmoke)
    {
        if(TracePath.IsEmpty()) TracePath=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("RollbackSmoke/ue-trace.json"));
        if(CaptureDirectory.IsEmpty()) CaptureDirectory=FPaths::Combine(FPaths::GetPath(TracePath),TEXT("captures"));
    }
    if(!TracePath.IsEmpty()) TracePath=FPaths::ConvertRelativePathToFull(TracePath);
    auto Mode=bSmoke||FParse::Param(FCommandLine::Get(),TEXT("RollbackLabAuto"))
        ? RollbackArena::EMode::AutoDemo:RollbackArena::EMode::Interactive;
    if(FParse::Param(FCommandLine::Get(),TEXT("RollbackLabDesync"))) Mode=RollbackArena::EMode::Desync;
    Settings=RollbackArena::FModel::DefaultSettings(Mode);
    uint64 Frames=Settings.FrameCount, Preset=Settings.NetworkPreset;
    if(!ReadUnsigned(TEXT("RollbackLabScenarioSeed"),Settings.ScenarioSeed,Failure)||
       !ReadUnsigned(TEXT("RollbackLabTransportSeed"),Settings.TransportSeed,Failure)||
       !ReadUnsigned(TEXT("RollbackLabFrames"),Frames,Failure)||
       !ReadUnsigned(TEXT("RollbackLabNetworkPreset"),Preset,Failure)) return false;
    if(Frames==0||Frames>36000||Preset>2) { Failure=TEXT("Frames/preset outside the bounded demo contract.");return false; }
    if(bSmoke&&(Mode!=RollbackArena::EMode::AutoDemo||Preset!=1)) { Failure=TEXT("Parity smoke requires the automatic default transport preset.");return false; }
    Settings.FrameCount=static_cast<uint32>(Frames);Settings.NetworkPreset=static_cast<uint32>(Preset);
    uint64 ExitSeconds=0;
    if(!ReadUnsigned(TEXT("RollbackLabExitAfterSeconds"),ExitSeconds,Failure))return false;
    if(ExitSeconds>7200) { Failure=TEXT("Invalid automatic exit interval.");return false; }
    ExitAfterSeconds=static_cast<double>(ExitSeconds);
    if(!CaptureDirectory.IsEmpty())
    {
        CaptureDirectory=FPaths::ConvertRelativePathToFull(CaptureDirectory);
        if(!IFileManager::Get().MakeDirectory(*CaptureDirectory,true)) { Failure=TEXT("Cannot create capture directory.");return false; }
    }
    return true;
}

void ARollbackArenaView::BeginPlay()
{
    Super::BeginPlay();StartedAt=FPlatformTime::Seconds();
    APlayerController* PC=GetWorld()->GetFirstPlayerController();Controller=PC;
    if(PC)
    {
        PC->bAutoManageActiveCameraTarget=false;PC->SetViewTarget(this);
        AddTickPrerequisiteActor(PC);
        PC->bShowMouseCursor=false;PC->SetInputMode(FInputModeGameOnly());
        if(ARollbackArenaHUD* Hud=Cast<ARollbackArenaHUD>(PC->GetHUD())) Hud->SetArena(this);
    }
    RollbackArena::FSettings Settings;
    if(!ReadSettings(Settings)) { Fail(Failure);return; }
    if(!InitializeVisuals()) { Fail(Failure);return; }
    Bridge=GetGameInstance()->GetSubsystem<URollbackLabSubsystem>();
    if(!Bridge) { Fail(TEXT("RollbackLab subsystem unavailable."));return; }
    Model=MakeUnique<RollbackArena::FModel>(Bridge->GetRuntime());
    const auto Started=Model->Start(Settings);
    if(!Started.IsOk()) { Fail(Started.Message);return; }
    if(!RequestedGitSha.IsEmpty()&&RequestedGitSha!=UTF8_TO_TCHAR(Model->Runtime().GetVersionInfo().source_git_sha))
    { Fail(TEXT("Requested source SHA differs from loaded SDK."));return; }
    ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this,&ARollbackArenaView::ScreenshotProcessed);
    UpdateProjection();
    UE_LOG(LogTemp,Display,TEXT("RollbackArena ready: independent SDK peers, mode=%s, ABI=%u"),
        RollbackArena::FModel::ModeName(Settings.Mode),RL_API_VERSION);
}

uint32 ARollbackArenaView::LocalButtons() const
{
    APlayerController* PC=Controller.Get();if(!PC)return 0;
    // A complete press/release can arrive between rendered frames. Retain that
    // press until an actual fixed step consumes it, including while paused.
    const auto Active=[PC](const FKey& Key){return PC->IsInputKeyDown(Key)||PC->WasInputKeyJustPressed(Key);};
    uint32 Bits=PendingButtons;
    if(Active(EKeys::W)||Active(EKeys::Up))Bits|=RL_BUTTON_UP;
    if(Active(EKeys::S)||Active(EKeys::Down))Bits|=RL_BUTTON_DOWN;
    if(Active(EKeys::A)||Active(EKeys::Left))Bits|=RL_BUTTON_LEFT;
    if(Active(EKeys::D)||Active(EKeys::Right))Bits|=RL_BUTTON_RIGHT;
    if(Active(EKeys::SpaceBar))Bits|=RL_BUTTON_ATTACK;
    return Bits;
}

void ARollbackArenaView::ReadControls(bool& bSkipClock)
{
    APlayerController* PC=Controller.Get();if(!PC)return;
    if(PC->WasInputKeyJustPressed(EKeys::Escape)) { if(bSmoke)Fail(TEXT("Smoke aborted by user."));else Quit(0);return; }
    if(bSmoke||!Model||!Failure.IsEmpty())return;
    auto& Runtime=Model->Runtime();
    if(PC->WasInputKeyJustPressed(EKeys::P))Model->SetPaused(!Runtime.GetClockState().bPaused);
    RollbackLabBridge::FResult Result;
    bool bReset=false;
    if(PC->WasInputKeyJustPressed(EKeys::R)){Result=Model->Reset();bReset=true;}
    else if(PC->WasInputKeyJustPressed(EKeys::N)){Result=Model->ChangePreset((Model->GetSettings().NetworkPreset+1U)%3U);bReset=true;}
    else if(PC->WasInputKeyJustPressed(EKeys::One)){Result=Model->Start(RollbackArena::FModel::DefaultSettings(RollbackArena::EMode::AutoDemo));bReset=true;}
    else if(PC->WasInputKeyJustPressed(EKeys::Two)){Result=Model->Start(RollbackArena::FModel::DefaultSettings(RollbackArena::EMode::Interactive));bReset=true;}
    else if(PC->WasInputKeyJustPressed(EKeys::Three)){Result=Model->Start(RollbackArena::FModel::DefaultSettings(RollbackArena::EMode::Desync));bReset=true;}
    else if(PC->WasInputKeyJustPressed(EKeys::Period)&&Runtime.IsRunning()){Result=Model->Step(PendingButtons);PendingButtons=0;bSkipClock=true;}
    if(!Result.IsOk()){Fail(Result.Message);return;}
    if(bReset)
    {
        SeenCorrection[0]=SeenCorrection[1]=0;CorrectionAge[0]=CorrectionAge[1]=10.0f;
        PendingButtons=0;
        bSkipClock=true;
    }
}

void ARollbackArenaView::UpdateProjection()
{
    if(!Model||!Failure.IsEmpty())return;
    for(uint32 Peer=0;Peer<2;++Peer)
    {
        const auto& View=Model->Runtime().GetPeer(Peer);
        for(uint32 Player=0;Player<2;++Player)
        {
            const auto& P=View.Snapshot.players[Player];
            Players[Peer*2+Player]->SetRelativeLocation(ProjectPosition(Peer,P.x,P.y));
            Players[Peer*2+Player]->SetVisibility(true);
        }
        for(uint32 Slot=0;Slot<64;++Slot)
        {
            const auto& P=View.Snapshot.projectiles[Slot];auto* Mesh=Projectiles[Peer*64+Slot].Get();
            Mesh->SetVisibility(P.active!=0);
            if(P.active)
            {
                Mesh->SetRelativeLocation(ProjectPosition(Peer,P.x,P.y,12));
                Mesh->SetMaterial(0,Materials[P.owner==0?4:5]);
            }
        }
        if(View.CorrectionRevision!=SeenCorrection[Peer])
        {SeenCorrection[Peer]=View.CorrectionRevision;CorrectionAge[Peer]=0.0f;}
    }
}
float ARollbackArenaView::CorrectionAlpha(uint32 Peer) const
{return Peer<2?FMath::Clamp(1.0f-CorrectionAge[Peer]/0.65f,0.0f,1.0f):0.0f;}

void ARollbackArenaView::RequestCapture(const FString& Name)
{
    if(bCapturePending||CaptureDirectory.IsEmpty())return;
    PendingCaptureName=Name;bCapturePending=true;
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(CaptureDirectory,Name+TEXT(".png")),true,false,false,FIntRect(),true);
}
void ARollbackArenaView::ScreenshotProcessed()
{
    if(!bCapturePending)return;
    bCapturePending=false;
    const FString Path=FPaths::Combine(CaptureDirectory,PendingCaptureName+TEXT(".png"));
    if(IFileManager::Get().FileSize(*Path)<=0){Fail(TEXT("Requested screenshot was not written: ")+Path);return;}
    if(PendingCaptureName==TEXT("start"))bStartCaptured=true;
    else if(PendingCaptureName==TEXT("correction"))bCorrectionCaptured=true;
    else bFinalCaptured=true;
}

bool ARollbackArenaView::SaveEvidence(bool bSuccess)
{
    if(TracePath.IsEmpty())return !bSmoke;
    FString Report,Trace;TArray<uint8> Replay;
    const bool HasModel=Model.IsValid();
    const bool HasReport=HasModel&&Model->Runtime().CopyReport(Report).IsOk();
    const bool HasTrace=HasModel&&Model->Runtime().CopyTrace(Trace).IsOk();
    const bool HasReplay=HasModel&&Model->Runtime().CopyReplay(Replay).IsOk();
    TSharedPtr<FJsonObject> Parsed;
    const bool ReportSuccess=HasReport&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Report),Parsed)&&Parsed.IsValid()&&Parsed->GetBoolField(TEXT("success"));
    const bool ReplayVerified=Parsed.IsValid()&&Parsed->GetBoolField(TEXT("replay_verification"));
    const auto* Runtime=HasModel?&Model->Runtime():nullptr;
    const auto* A=Runtime?&Runtime->GetPeer(0):nullptr;const auto* B=Runtime?&Runtime->GetPeer(1):nullptr;
    const auto Settings=HasModel?Model->GetSettings():RollbackArena::FSettings{};
    const uint64 Rollbacks=A&&B?static_cast<uint64>(A->Metrics.rollback_count)+B->Metrics.rollback_count:0;
    bSuccess=bSuccess&&HasReport&&HasReplay&&ReportSuccess&&ReplayVerified&&bStartCaptured&&bCorrectionCaptured&&bFinalCaptured&&
        A&&B&&A->Metrics.confirmed_frame==Settings.FrameCount&&B->Metrics.confirmed_frame==Settings.FrameCount&&
        A->Snapshot.state_hash==B->Snapshot.state_hash&&Rollbacks>0;
    if(!bSuccess&&Failure.IsEmpty())Failure=TEXT("Smoke did not satisfy report, replay, screenshot, rollback and confirmed-parity invariants.");
    FString Json=TEXT("{\n  \"schema_version\":1,\n  \"source_git_sha\":")+
        Quoted(Runtime?UTF8_TO_TCHAR(Runtime->GetVersionInfo().source_git_sha):TEXT("unavailable"));
    Json+=TEXT(",\n  \"sdk_version\":\"0.2.0-candidate\",\n  \"abi_version\":1,");
    Json+=FString::Printf(TEXT("\n  \"scenario_seed\":%llu,\n  \"transport_seed\":%llu,\n  \"target_frame\":%u,\n  \"logical_tick\":%u,"),
        Settings.ScenarioSeed,Settings.TransportSeed,Settings.FrameCount,Runtime?Runtime->GetLastStep().logical_tick:0);
    Json+=FString::Printf(TEXT("\n  \"confirmed_a\":%u,\n  \"confirmed_b\":%u,\n  \"final_hash_a\":%s,\n  \"final_hash_b\":%s,\n  \"rollback_count\":%llu,"),
        A?A->Metrics.confirmed_frame:0,B?B->Metrics.confirmed_frame:0,*Quoted(Hex(A?A->Snapshot.state_hash:0)),*Quoted(Hex(B?B->Snapshot.state_hash:0)),Rollbacks);
    Json+=TEXT("\n  \"replay_verified\":")+FString(ReplayVerified?TEXT("true"):TEXT("false"));
    Json+=TEXT(",\n  \"success\":")+FString(bSuccess?TEXT("true"):TEXT("false"));
    Json+=TEXT(",\n  \"failure_reason\":")+Quoted(Failure);
    Json+=TEXT(",\n  \"core_report\":")+(HasReport?Report:TEXT("null"));
    Json+=TEXT(",\n  \"core_trace\":")+(HasTrace?Trace:TEXT("null"));
    Json+=TEXT("\n}\n");
    const FString Directory=FPaths::GetPath(TracePath);
    bool Written=IFileManager::Get().MakeDirectory(*Directory,true);
    if(HasReport)Written=FFileHelper::SaveStringToFile(Report,*FPaths::Combine(Directory,TEXT("report.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&Written;
    if(HasReplay)Written=FFileHelper::SaveArrayToFile(Replay,*FPaths::Combine(Directory,TEXT("input.rlr")))&&Written;
    Written=FFileHelper::SaveStringToFile(Json,*TracePath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&Written;
    bWritten=true;
    UE_LOG(LogTemp,Display,TEXT("RollbackLab smoke: success=%s hash=%s rollbacks=%llu trace=%s"),bSuccess?TEXT("true"):TEXT("false"),*Hex(A?A->Snapshot.state_hash:0),Rollbacks,*TracePath);
    return Written&&bSuccess;
}

void ARollbackArenaView::Fail(const FString& Reason)
{
    Failure=Reason;UE_LOG(LogTemp,Error,TEXT("RollbackArena failure: %s"),*Failure);
    if(bSmoke&&!bWritten){SaveEvidence(false);Quit(1);}
}
void ARollbackArenaView::Quit(uint8 Status)
{
    if(bExiting)return;bExiting=true;
    if(bSmoke&&!bWritten)
    {
        Failure=TEXT("Exit requested before smoke completion.");
        SaveEvidence(false);Status=1;
    }
    if(Status!=0)
    {
        // Windows' graceful PostQuitMessage path can lose the status when the
        // engine exit flag ends the loop first. Release our resources and flush
        // evidence before requesting an explicit nonzero process termination.
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
        if(bCapturePending)FScreenshotRequest::Reset();
        bCapturePending=false;
        if(Bridge)Bridge->GetRuntime().Stop();
        FPlatformMisc::RequestExitWithStatus(true,Status,TEXT("RollbackArena verified failure"));
        return;
    }
    FPlatformMisc::RequestExitWithStatus(false,Status,TEXT("RollbackArena"));
}
void ARollbackArenaView::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    for(float& Age:CorrectionAge)Age=FMath::Min(10.0f,Age+DeltaSeconds);
    PendingButtons=LocalButtons();
    bool SkipClock=false;ReadControls(SkipClock);
    if(bExiting)return;
    const double Elapsed=FPlatformTime::Seconds()-StartedAt;
    if(bSmoke&&Elapsed>45.0){Fail(TEXT("Smoke watchdog expired."));return;}
    bool ShadersReady=true;
#if WITH_EDITOR
    ShadersReady=!GShaderCompilingManager||!GShaderCompilingManager->IsCompiling();
#endif
    if(ExitAfterSeconds>0&&Elapsed>=ExitAfterSeconds&&!bCapturePending&&ShadersReady){Quit(0);return;}
    if(!Failure.IsEmpty()||!Model)return;
    if(!CaptureDirectory.IsEmpty()&&!bStartCaptured)
    {
        UpdateProjection();
#if WITH_EDITOR
        if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())return;
#endif
        // Cooked builds can defer scene-proxy creation until asynchronous PSO
        // precaching completes. HUD readiness alone does not prove meshes draw.
        bool PresentationReady=true;
        for(const auto& Mesh:Floors)
            PresentationReady&=!Mesh->IsPSOPrecaching()&&Mesh->GetSceneProxy()!=nullptr&&!Mesh->IsRenderStateDirty();
        for(const auto& Mesh:Players)
            PresentationReady&=!Mesh->IsPSOPrecaching()&&Mesh->GetSceneProxy()!=nullptr&&!Mesh->IsRenderStateDirty();
        if(!PresentationReady){WarmupTicks=3;return;}
        if(!bCapturePending){if(WarmupTicks>0)--WarmupTicks;else RequestCapture(TEXT("start"));}
        return;
    }
    if(bCapturePending)return;
    if(!SkipClock&&Model->Runtime().IsRunning())
    {
        const auto Result=Model->Tick(static_cast<double>(DeltaSeconds),PendingButtons);
        if(Model->Runtime().GetClockState().LastTickSteps>0)PendingButtons=0;
        if(!Result.IsOk()){Fail(Result.Message);return;}
    }
    UpdateProjection();
    if(!CaptureDirectory.IsEmpty()&&!bCorrectionCaptured&&(SeenCorrection[0]||SeenCorrection[1]))
    {RequestCapture(TEXT("correction"));return;}
    if(Model->Runtime().IsFinished()&&!CaptureDirectory.IsEmpty()&&!bFinalCaptured)
    {RequestCapture(Model->Runtime().GetLastStep().desync_detected?TEXT("desync"):TEXT("convergence"));return;}
    if(bSmoke&&Model->Runtime().IsFinished()&&bFinalCaptured&&!bWritten)
    {const bool Success=SaveEvidence(true);Quit(Success?0:1);}
}
void ARollbackArenaView::EndPlay(const EEndPlayReason::Type Reason)
{
    const bool IncompleteSmoke=bSmoke&&!bWritten;
    if(IncompleteSmoke){Failure=TEXT("World ended before smoke completion.");SaveEvidence(false);}
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
    if(bCapturePending)FScreenshotRequest::Reset();
    bCapturePending=false;Model.Reset();
    if(Bridge)Bridge->GetRuntime().Stop();
    Bridge=nullptr;
    Super::EndPlay(Reason);
    if(IncompleteSmoke)FPlatformMisc::RequestExitWithStatus(true,1,TEXT("RollbackArena incomplete smoke"));
}
