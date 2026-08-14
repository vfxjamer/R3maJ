#include "R3maJ.h"
#include "PhaseManager.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace RocketSim;
static PhaseManager g_PhaseManager; static int g_phaseIdx=0;
static void PrintUsage(const char* p){RG_LOG("Usage: "<<p<<" [--device cpu|cuda] [--games n] [--save-dir dir] [--phase n] [--replays path]");}
int main(int argc,char*argv[]){
    SetupSignalHandlers(); int phaseIdx=-1; std::string saveDir="checkpoints",replayPath,deviceStr="cpu"; int numGames=-1;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){PrintUsage(argv[0]);return 0;}
        else if(!strcmp(argv[i],"--device")&&i+1<argc)deviceStr=argv[++i];
        else if(!strcmp(argv[i],"--games")&&i+1<argc)numGames=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--save-dir")&&i+1<argc)saveDir=argv[++i];
        else if(!strcmp(argv[i],"--phase")&&i+1<argc){phaseIdx=atoi(argv[++i]);phaseIdx=std::clamp(phaseIdx,0,3);}
        else if(!strcmp(argv[i],"--replays")&&i+1<argc)replayPath=argv[++i];
        else {RG_LOG("Unknown option: "<<argv[i]);PrintUsage(argv[0]);return 1;}
    }
    RocketSim::Init("collision_meshes");
    uint64_t total=ReadCheckpointTotalTimesteps(saveDir); if(phaseIdx<0)phaseIdx=g_PhaseManager.GetCurrentPhase(total); g_phaseIdx=phaseIdx;
    g_rewards=g_PhaseManager.GetRewards(total); g_replayPath=replayPath;
    LearnerConfig cfg=g_PhaseManager.MakeLearnerConfig(phaseIdx); cfg.checkpointFolder=saveDir;
    cfg.deviceType=(deviceStr=="cuda")?GGL::LearnerDeviceType::GPU_CUDA:GGL::LearnerDeviceType::CPU; if(numGames>0)cfg.numGames=numGames;
    const auto&r=g_rewards; const auto&pc=g_PhaseManager.GetPhaseConfig(phaseIdx);
    RG_LOG(RG_DIVIDER); RG_LOG("=== R3maJ v3 — Custom Reward Training ===");
    RG_LOG("  Timesteps at start: "<<total); RG_LOG("  Phase: "<<(phaseIdx+1)<<"/4"); RG_LOG("  Phase range: "<<pc.startStep<<" - "<<pc.endStep);
    RG_LOG("  Device: "<<(deviceStr=="cuda"?"CUDA":"CPU")<<"  Games: "<<cfg.numGames<<"  Tick Skip: "<<cfg.tickSkip<<"  Action Delay: "<<cfg.actionDelay);
    RG_LOG("  Network: shared 512x2 -> policy/critic 512x6  Epochs: "<<cfg.ppo.epochs<<"  LR: "<<cfg.ppo.policyLR<<"  MiniBatch: "<<cfg.ppo.miniBatchSize);
    RG_LOG("  Rewards: touch="<<r.touch_w<<" speedball="<<r.speed_to_ball_w<<" face="<<r.face_ball_w<<" air="<<r.air_time_w<<" groundidle="<<r.ground_idle_w);
    RG_LOG("           ballgoal="<<r.ball_to_goal_w<<" touchaccel="<<r.touch_accel_w<<" touchheight="<<r.touch_height_w<<" goal="<<r.goal_w<<" save="<<r.save_w);
    RG_LOG("           boostgain="<<r.boost_gain_w<<" saveboost="<<r.save_boost_w<<" boostwaste="<<r.boost_waste_w<<" demo="<<r.demo_w);
    RG_LOG("           airtouch="<<r.air_touch_w<<" flipreset="<<r.flip_reset_w<<" walltouch="<<r.wall_touch_w<<" airdribble="<<r.air_dribble_w<<" cradle="<<r.cradle_w<<" angular="<<r.angular_movement_w);
    RG_LOG("  Goal outcome: +"<<r.goal_w<<" score / -"<<r.goal_w*.8f<<" concede (20% aggression bias)");
    Learner* learner=new Learner(EnvCreateFunc,cfg,StepCallback); RG_LOG("  Actions: "<<learner->numActions); RG_LOG("  Obs Size: "<<learner->obsSize); RG_LOG(RG_DIVIDER); learner->Start(); delete learner; return 0;
}
