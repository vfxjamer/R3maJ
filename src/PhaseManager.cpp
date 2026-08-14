#include "PhaseManager.h"
#include <GigaLearnCPP/PPO/PPOLearnerConfig.h>
#include <GigaLearnCPP/Util/ModelConfig.h>
#include <algorithm>

static constexpr int64_t P0=5'000'000'000, P1=15'000'000'000, P2=30'000'000'000, P3=60'000'000'000;
static PhaseRewards Lerp(const PhaseRewards& a,const PhaseRewards& b,float f){
    f=std::clamp(f,0.f,1.f); PhaseRewards r=a;
    auto mix=[&](float& x,float y){x=x+(y-x)*f;};
    mix(r.touch_w,b.touch_w); mix(r.speed_to_ball_w,b.speed_to_ball_w); mix(r.face_ball_w,b.face_ball_w);
    mix(r.air_time_w,b.air_time_w); mix(r.ground_idle_w,b.ground_idle_w); mix(r.ball_to_goal_w,b.ball_to_goal_w);
    mix(r.touch_accel_w,b.touch_accel_w); mix(r.touch_height_w,b.touch_height_w); mix(r.goal_w,b.goal_w); mix(r.save_w,b.save_w);
    mix(r.boost_gain_w,b.boost_gain_w); mix(r.save_boost_w,b.save_boost_w); mix(r.boost_waste_w,b.boost_waste_w); mix(r.demo_w,b.demo_w);
    mix(r.air_touch_w,b.air_touch_w); mix(r.flip_reset_w,b.flip_reset_w); mix(r.wall_touch_w,b.wall_touch_w);
    mix(r.air_dribble_w,b.air_dribble_w); mix(r.cradle_w,b.cradle_w); mix(r.angular_movement_w,b.angular_movement_w);
    return r;
}
PhaseManager::PhaseManager(){
    PhaseRewards p0={5.f,2.f,.25f,.10f,.05f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f,0.f};
    PhaseRewards p1={.50f,1.f,.10f,.10f,.02f,3.f,1.f,.20f,10.f,.50f,.10f,.05f,.05f,.10f,.25f,0.f,0.f,0.f,0.f,0.f};
    PhaseRewards p2={.25f,.50f,.05f,.05f,.015f,4.f,1.25f,.35f,10.f,.75f,.15f,.10f,.08f,.15f,.50f,0.f,0.f,0.f,0.f,.001f};
    PhaseRewards p3=p2;
    p3.touch_w=.15f; p3.touch_accel_w=1.5f; p3.touch_height_w=.50f; p3.save_w=1.f;
    p3.boost_gain_w=.20f; p3.save_boost_w=.12f; p3.boost_waste_w=.10f; p3.demo_w=.20f; p3.air_touch_w=1.f;
    p3.flip_reset_w=1.5f; p3.wall_touch_w=.35f; p3.air_dribble_w=.35f; p3.cradle_w=.15f; p3.angular_movement_w=.0025f;
    _phases[0]={0,P0,.990f,.05f,2e-4f,2e-4f,p0};
    _phases[1]={P0,P1,.993f,.03f,1e-4f,1e-4f,p1};
    _phases[2]={P1,P2,.995f,.02f,1e-4f,1e-4f,p2};
    _phases[3]={P2,P3,.997f,.01f,1e-4f,1e-4f,p3};
}
int PhaseManager::GetCurrentPhase(int64_t s)const{for(int i=0;i<4;i++)if(s>=_phases[i].startStep&&s<_phases[i].endStep)return i;return 3;}
const PhaseConfig& PhaseManager::GetPhaseConfig(int i)const{return _phases[std::clamp(i,0,3)];}
PhaseRewards PhaseManager::GetRewards(int64_t s)const{
    if(s<P2)return _phases[GetCurrentPhase(s)].rewards;
    float f=(float)(s-P2)/(float)(P3-P2);
    return Lerp(_phases[2].rewards,_phases[3].rewards,f);
}
GGL::LearnerConfig PhaseManager::MakeLearnerConfig(int phaseIdx)const{
    const auto&p=_phases[std::clamp(phaseIdx,0,3)]; GGL::LearnerConfig cfg={};
    static const int64_t BATCH[4]={100'000,200'000,300'000,400'000}; int64_t batch=BATCH[std::clamp(phaseIdx,0,3)];
    cfg.numGames=96; cfg.tickSkip=8; cfg.actionDelay=2; cfg.deviceType=GGL::LearnerDeviceType::CPU;
    cfg.checkpointFolder="checkpoints"; cfg.tsPerSave=5'000'000; cfg.checkpointsToKeep=8; cfg.standardizeObs=true; cfg.standardizeReturns=true;
    cfg.addRewardsToMetrics=true; cfg.sendMetrics=false; cfg.renderMode=false; cfg.randomSeed=-1;
    auto&ppo=cfg.ppo; ppo.tsPerItr=batch; ppo.batchSize=batch; ppo.miniBatchSize=batch/10; ppo.epochs=15; ppo.gaeLambda=.95f;
    ppo.gaeGamma=p.gamma; ppo.entropyScale=p.entropyScale; ppo.policyLR=p.policyLR; ppo.criticLR=p.criticLR; ppo.clipRange=.2f;
    ppo.rewardClipRange=10.f; ppo.policyTemperature=1.f; ppo.maxEpisodeDuration=120;
    ppo.sharedHead.layerSizes={512,512}; ppo.sharedHead.addLayerNorm=true; ppo.sharedHead.activationType=GGL::ModelActivationType::RELU; ppo.sharedHead.optimType=GGL::ModelOptimType::ADAM;
    ppo.policy.layerSizes={512,512,512,512,512,512}; ppo.policy.addLayerNorm=true; ppo.policy.activationType=GGL::ModelActivationType::RELU; ppo.policy.optimType=GGL::ModelOptimType::ADAM;
    ppo.critic.layerSizes={512,512,512,512,512,512}; ppo.critic.addLayerNorm=true; ppo.critic.activationType=GGL::ModelActivationType::RELU; ppo.critic.optimType=GGL::ModelOptimType::ADAM;
    return cfg;
}
