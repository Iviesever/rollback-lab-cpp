#include "RollbackArenaHUD.h"
#include "RollbackArenaView.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"

namespace
{
const FLinearColor Ink(0.73f,0.82f,0.91f);
const FLinearColor Muted(0.32f,0.45f,0.57f);
const FLinearColor Cyan(0.12f,0.95f,0.92f);
const FLinearColor Coral(1.0f,0.31f,0.36f);
const FLinearColor Gold(1.0f,0.7f,0.25f);
const TCHAR* UdpPhase(uint32 Phase)
{
    switch(Phase)
    {
    case RL_UDP_HANDSHAKE:return TEXT("HANDSHAKE");
    case RL_UDP_RUNNING:return TEXT("RUNNING");
    case RL_UDP_CONFIRMING:return TEXT("CONFIRMING");
    case RL_UDP_FINISHED:return TEXT("CONVERGED");
    case RL_UDP_FAILED:return TEXT("FAILED");
    default:return TEXT("UNKNOWN");
    }
}
FString HashText(uint64 Value){return FString::Printf(TEXT("0x%016llX"),Value);}
}

void ARollbackArenaHUD::Label(const FString& Text,float X,float Y,float Size,FLinearColor Color)
{
    DrawText(Text,Color,X,Y,GEngine->GetMediumFont(),Size/12.0f,false);
}
void ARollbackArenaHUD::WorldLine(const FVector& A,const FVector& B,FLinearColor Color,float Width)
{
    FVector2D P,Q;
    if(PlayerOwner&&PlayerOwner->ProjectWorldLocationToScreen(A,P,true)&&PlayerOwner->ProjectWorldLocationToScreen(B,Q,true))
        DrawLine(P.X,P.Y,Q.X,Q.Y,Color,Width);
}

void ARollbackArenaHUD::PeerPanel(uint32 Peer,float S)
{
    const auto* Model=Arena->GetModel();if(!Model)return;
    const auto& Runtime=Model->Runtime();const auto& View=Runtime.GetPeer(Peer);
    const float X=(Runtime.IsUdp()?420.0f:(Peer==0?80.0f:850.0f))*S;
    const FLinearColor Accent=Peer==0?Cyan:Coral;
    const bool Confirmed=View.Metrics.confirmed_frame==View.Snapshot.frame;
    Label(Runtime.IsUdp()?(Peer==0?TEXT("UDP A"):TEXT("UDP B")):(Peer==0?TEXT("PEER A"):TEXT("PEER B")),X,202*S,20*S,Accent);
    Label(Confirmed?TEXT("CONFIRMED STATE"):TEXT("PREDICTED STATE"),X+124*S,207*S,12*S,Confirmed?Cyan:Gold);
    Label(HashText(View.Snapshot.state_hash),X+376*S,205*S,15*S,Ink);

    for(uint32 Column=0;Column<=8;++Column)
        WorldLine(ARollbackArenaView::ProjectPosition(Peer,static_cast<int32>(Column*128U*1024U),0,0),
                  ARollbackArenaView::ProjectPosition(Peer,static_cast<int32>(Column*128U*1024U),576*1024,0),FLinearColor(0.07f,0.15f,0.21f),1*S);
    for(uint32 Row=0;Row<=6;++Row)
        WorldLine(ARollbackArenaView::ProjectPosition(Peer,0,static_cast<int32>(Row*96U*1024U),0),
                  ARollbackArenaView::ProjectPosition(Peer,1024*1024,static_cast<int32>(Row*96U*1024U),0),FLinearColor(0.07f,0.15f,0.21f),1*S);
    const float Flash=Arena->CorrectionAlpha(Peer);
    const FLinearColor Border=Flash>0?FLinearColor(1,0.72f,0.28f,Flash):FLinearColor(0.12f,0.25f,0.33f);
    const FVector Corners[]={ARollbackArenaView::ProjectPosition(Peer,0,0),ARollbackArenaView::ProjectPosition(Peer,1024*1024,0),
        ARollbackArenaView::ProjectPosition(Peer,1024*1024,576*1024),ARollbackArenaView::ProjectPosition(Peer,0,576*1024)};
    for(int32 Edge=0;Edge<4;++Edge)WorldLine(Corners[Edge],Corners[(Edge+1)%4],Border,Flash>0?3*S:1*S);

    uint32 ProjectileCount=0;
    for(const auto& Projectile:View.Snapshot.projectiles)ProjectileCount+=Projectile.active?1U:0U;
    for(uint32 Player=0;Player<2;++Player)
    {
        const auto& P=View.Snapshot.players[Player];FVector2D Screen;
        if(PlayerOwner->ProjectWorldLocationToScreen(ARollbackArenaView::ProjectPosition(Peer,P.x,P.y),Screen,true))
        {
            const FLinearColor Color=Player==0?Cyan:Coral;
            Label(Player==0?TEXT("A"):TEXT("B"),Screen.X-5*S,Screen.Y-28*S,15*S,Color);
            DrawRect(FLinearColor(0.02f,0.06f,0.09f),Screen.X-17*S,Screen.Y+14*S,34*S,3*S);
            DrawRect(Color,Screen.X-17*S,Screen.Y+14*S,34*S*FMath::Clamp(static_cast<float>(P.hp)/100.0f,0.0f,1.0f),3*S);
        }
        if(Flash>0&&View.LastCorrection.performed)
        {
            const auto& Before=View.LastCorrection.before.players[Player];const auto& After=View.LastCorrection.after.players[Player];
            const FVector A=ARollbackArenaView::ProjectPosition(Peer,Before.x,Before.y,12);
            const FVector B=ARollbackArenaView::ProjectPosition(Peer,After.x,After.y,12);
            WorldLine(A,B,FLinearColor(1,0.72f,0.28f,Flash),2*S);
            FVector2D Ghost,Corrected;
            if(PlayerOwner->ProjectWorldLocationToScreen(A,Ghost,true)&&PlayerOwner->ProjectWorldLocationToScreen(B,Corrected,true))
            {
                const float Radius=9*S;const FLinearColor GhostColor(0.95f,0.76f,0.45f,Flash);
                DrawLine(Ghost.X-Radius,Ghost.Y-Radius,Ghost.X+Radius,Ghost.Y-Radius,GhostColor,1.5f*S);
                DrawLine(Ghost.X+Radius,Ghost.Y-Radius,Ghost.X+Radius,Ghost.Y+Radius,GhostColor,1.5f*S);
                DrawLine(Ghost.X+Radius,Ghost.Y+Radius,Ghost.X-Radius,Ghost.Y+Radius,GhostColor,1.5f*S);
                DrawLine(Ghost.X-Radius,Ghost.Y+Radius,Ghost.X-Radius,Ghost.Y-Radius,GhostColor,1.5f*S);
                DrawRect(GhostColor,Corrected.X-2*S,Corrected.Y-2*S,4*S,4*S);
            }
        }
    }
    const float Y=687*S;
    Label(TEXT("PREDICTED"),X,Y,11*S,Muted);Label(TEXT("CONFIRMED"),X+178*S,Y,11*S,Muted);
    Label(TEXT("ROLLBACKS"),X+356*S,Y,11*S,Muted);Label(TEXT("RESIM FRAMES"),X+534*S,Y,11*S,Muted);
    Label(FString::FromInt(View.Snapshot.frame),X,Y+22*S,28*S,Ink);
    Label(FString::FromInt(View.Metrics.confirmed_frame),X+178*S,Y+22*S,28*S,Cyan);
    Label(FString::FromInt(View.Metrics.rollback_count),X+356*S,Y+22*S,28*S,Gold);
    Label(FString::Printf(TEXT("%llu"),View.Metrics.total_resimulated_frames),X+534*S,Y+22*S,28*S,Ink);
    Label(FString::Printf(TEXT("LAST CORRECTION  %u  /  DEPTH %u"),View.LastCorrection.rollback_from,View.LastCorrection.resimulated_frames),X,Y+70*S,13*S,Flash>0?Gold:Muted);
    Label(FString::Printf(TEXT("HP %d / %d    SCORE %u / %u    PROJECTILES %u"),View.Snapshot.players[0].hp,View.Snapshot.players[1].hp,
        View.Snapshot.players[0].score,View.Snapshot.players[1].score,ProjectileCount),X,Y+98*S,12*S,Muted);
}

void ARollbackArenaHUD::DrawHUD()
{
    Super::DrawHUD();if(!Canvas)return;
    const float S=FMath::Min(Canvas->SizeX/1600.0f,Canvas->SizeY/900.0f);
    DrawRect(FLinearColor(0.012f,0.021f,0.034f),0,0,Canvas->SizeX,184*S);
    DrawRect(FLinearColor(0.012f,0.021f,0.034f),0,673*S,Canvas->SizeX,227*S);
    Label(TEXT("ROLLBACK LAB"),80*S,32*S,30*S,Ink);
    const auto* Model=Arena?Arena->GetModel():nullptr;
    const bool Udp=Model&&Model->GetSettings().Mode==RollbackArena::EMode::UdpPeer;
    Label(Udp?TEXT("UE 5.8 LIVE INTEGRATION  /  ONE LOCAL PEER WORLD"):
        TEXT("UE 5.8 LIVE INTEGRATION  /  TWO INDEPENDENT PEER WORLDS"),82*S,77*S,12*S,Muted);
    if(!Arena)return;
    const bool ConfirmedUdpDesync=Udp&&Model->Runtime().GetLastUdpStep().desync_detected!=0;
    if(!Arena->GetFailure().IsEmpty()&&!ConfirmedUdpDesync)
    {
        Label(TEXT("INTEGRATION FAILURE"),80*S,250*S,26*S,Coral);
        Label(Arena->GetFailure(),80*S,298*S,16*S,Ink);
        Label(TEXT("ESC  QUIT"),80*S,850*S,13*S,Muted);return;
    }
    if(!Model)return;
    const auto& Runtime=Model->Runtime();const auto& Settings=Model->GetSettings();const auto& Net=Model->GetOptions().Scenario;
    const auto& Step=Runtime.GetLastStep();
    FString Status=Runtime.IsFinished()?TEXT("CONFIRMED CONVERGENCE"):(Runtime.GetClockState().bPaused?TEXT("PAUSED"):RollbackArena::FModel::ModeName(Settings.Mode));
    if(Udp)Status=FString::Printf(TEXT("UDP %s  /  %s"),Settings.LocalPeer==RL_PEER_A?TEXT("A"):TEXT("B"),UdpPhase(Runtime.GetLastUdpStep().phase));
    if(Step.desync_detected)Status=FString::Printf(TEXT("CONFIRMED DESYNC  /  FRAME %u"),Step.earliest_divergent_frame);
    Label(Status,980*S,36*S,18*S,Step.desync_detected?Coral:Cyan);
    Label(FString::Printf(TEXT("LOGICAL TICK %u   /   SDK 0.2.0   ABI %u"),Step.logical_tick,Runtime.GetVersionInfo().api_version),980*S,76*S,12*S,Muted);
    if(Udp)
        Label(FString::Printf(TEXT("UDP %s    RELAY 127.0.0.1:%u    ENGINE-UDP-V1 / ABI PROFILE %u    %s"),
            Settings.LocalPeer==RL_PEER_A?TEXT("A"):TEXT("B"),Settings.RelayPort,
            Settings.HelloAbi?Settings.HelloAbi:RL_API_VERSION,UdpPhase(Runtime.GetLastUdpStep().phase)),80*S,116*S,13*S,Ink);
    else
        Label(FString::Printf(TEXT("%s    LATENCY %uT / %ums    JITTER +/- %uT    LOSS %u%%    REORDER %u%%    DUPLICATE %u%%"),
            RollbackArena::FModel::PresetName(Settings.NetworkPreset),Net.base_latency_ticks,Net.base_latency_ticks*1000U/60U,Net.jitter_ticks,Net.loss_percent,Net.reorder_percent,Net.duplicate_percent),80*S,116*S,13*S,Ink);
    Label(FString::Printf(TEXT("SCENARIO SEED  %llu     TRANSPORT SEED  %llu"),Settings.ScenarioSeed,Settings.TransportSeed),80*S,150*S,12*S,Muted);
    for(uint32 Peer=0;Peer<2;++Peer)if(Runtime.IsPeerActive(Peer))PeerPanel(Peer,S);
    DrawLine(80*S,838*S,1520*S,838*S,FLinearColor(0.1f,0.18f,0.25f),1*S);
    Label(Udp?TEXT("SCRIPTED UDP SCENARIO    /    ESC ABORT"):
        TEXT("1 AUTO   2 INTERACTIVE   3 DESYNC     WASD / ARROWS MOVE   SPACE FIRE     P PAUSE   . STEP   R RESET   N NETWORK   ESC QUIT"),80*S,859*S,12*S,Ink);
    if(Runtime.GetClockState().DiscardedSeconds>0.01)
        Label(FString::Printf(TEXT("WALL DEBT DISCARDED %.2fs"),Runtime.GetClockState().DiscardedSeconds),1190*S,151*S,11*S,Gold);
}
