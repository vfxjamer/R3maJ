#include "PhaseManager.h"
#include <GigaLearnCPP/PPO/PPOLearnerConfig.h>
#include <GigaLearnCPP/Util/ModelConfig.h>
#include <algorithm>

// Phase boundaries (in steps)
static constexpr int64_t PHASE_0_END = 5'000'000'000;
static constexpr int64_t PHASE_1_END = 15'000'000'000;
static constexpr int64_t PHASE_2_END = 30'000'000'000;
static constexpr int64_t PHASE_3_END = 200'000'000'000;

static constexpr int64_t MECH_RAMP_START = PHASE_2_END;
static constexpr int64_t MECH_RAMP_END   = 60'000'000'000;

static constexpr float MECH_TARGET_FLIP_RESET  = 10.f;
static constexpr float MECH_TARGET_JUMP_TOUCH  = 6.f;
static constexpr float MECH_TARGET_WALL_TOUCH  = 2.f;
static constexpr float MECH_TARGET_AIR_DRIBBLE = 2.f;
static constexpr float MECH_TARGET_CRADLE      = 1.f;
static constexpr float MECH_TARGET_ANG_VEL     = 0.005f;

PhaseManager::PhaseManager() {
    // Phase 1 / early stage follows ZealanL's RLGym-PPO guide exactly:
    // EventReward(touch=1), 50
    // SpeedTowardBallReward(), 5
    // FaceBallReward(), 1
    // AirReward(), 0.15
    //
    // Deliberately NO goal, goal-distance, win-probability, or scoring
    // rewards here. The guide explicitly recommends learning reliable ball
    // contact first because scoring rewards add noise before the bot can
    // consistently hit the ball.
    PhaseRewards phase0Rewards = {
        .boost_gain_w=0.f,.boost_lose_w=0.f,.ang_vel_w=0.f,.touch_grass_w=0.f,
        .goal_w=0.f,.win_prob_w=0.f,.goal_dist_w=0.f,.goal_speed_bonus_w=0.f,
        .touch_height_w=0.f,.touch_accel_w=0.f,.flip_reset_w=0.f,.demo_w=0.f,
        .opponent_punish_w=1.f,.goal_dist_bonus_w=0.f,.dist_w=0.f,.align_w=0.f,
        .jump_touch_w=0.f,.wall_touch_w=0.f,.air_dribble_w=0.f,.cradle_w=0.f,
    };

    PhaseRewards phase1Rewards = {
        .boost_gain_w=0.f,.boost_lose_w=0.f,.ang_vel_w=0.f,.touch_grass_w=0.f,
        .goal_w=0.f,.win_prob_w=0.f,.goal_dist_w=0.f,.goal_speed_bonus_w=0.f,
        .touch_height_w=0.f,.touch_accel_w=0.f,.flip_reset_w=0.f,.demo_w=0.f,
        .opponent_punish_w=1.f,.goal_dist_bonus_w=0.f,.dist_w=0.f,.align_w=0.f,
        .jump_touch_w=0.f,.wall_touch_w=0.f,.air_dribble_w=0.f,.cradle_w=0.f,
    };

    PhaseRewards phase2Rewards = {
        .boost_gain_w=0.5f,.boost_lose_w=0.25f,.ang_vel_w=0.f,.touch_grass_w=0.f,
        .goal_w=10.f,.win_prob_w=2.f,.goal_dist_w=5.f,.goal_speed_bonus_w=2.f,
        .touch_height_w=1.f,.touch_accel_w=1.f,.flip_reset_w=0.f,.demo_w=0.5f,
        .opponent_punish_w=1.f,.goal_dist_bonus_w=0.f,.dist_w=0.25f,.align_w=0.25f,
        .jump_touch_w=0.f,.wall_touch_w=0.f,.air_dribble_w=0.f,.cradle_w=0.f,
    };

    PhaseRewards phase3Rewards = phase2Rewards;
    phase3Rewards.flip_reset_w=MECH_TARGET_FLIP_RESET;
    phase3Rewards.jump_touch_w=MECH_TARGET_JUMP_TOUCH;
    phase3Rewards.wall_touch_w=MECH_TARGET_WALL_TOUCH;
    phase3Rewards.air_dribble_w=MECH_TARGET_AIR_DRIBBLE;
    phase3Rewards.cradle_w=MECH_TARGET_CRADLE;
    phase3Rewards.ang_vel_w=MECH_TARGET_ANG_VEL;

    _phases[0] = {0,PHASE_0_END,0.993f,0.05f,2e-4f,2e-4f,phase0Rewards};
    _phases[1] = {PHASE_0_END,PHASE_1_END,0.993f,0.03f,1e-4f,1e-4f,phase1Rewards};
    _phases[2] = {PHASE_1_END,PHASE_2_END,0.995f,0.02f,1e-4f,1e-4f,phase2Rewards};
    _phases[3] = {PHASE_2_END,PHASE_3_END,0.998f,0.01f,1e-4f,1e-4f,phase3Rewards};
}

int PhaseManager::GetCurrentPhase(int64_t totalTimesteps) const {
    for (int i=0;i<4;i++) {
        if (totalTimesteps >= _phases[i].startStep && totalTimesteps < _phases[i].endStep)
            return i;
    }
    return 3;
}

const PhaseConfig& PhaseManager::GetPhaseConfig(int phaseIdx) const { return _phases[phaseIdx]; }

PhaseRewards PhaseManager::GetRewards(int64_t totalTimesteps) const {
    int idx=GetCurrentPhase(totalTimesteps);
    if (idx!=3) return _phases[idx].rewards;
    PhaseRewards r=_phases[3].rewards;
    float f=0.f;
    if (totalTimesteps>MECH_RAMP_START) {
        f=(float)(totalTimesteps-MECH_RAMP_START)/(float)(MECH_RAMP_END-MECH_RAMP_START);
        f=std::clamp(f,0.0f,1.0f);
    }
    r.flip_reset_w*=f; r.jump_touch_w*=f; r.wall_touch_w*=f;
    r.air_dribble_w*=f; r.cradle_w*=f; r.ang_vel_w*=f;
    return r;
}

GGL::LearnerConfig PhaseManager::MakeLearnerConfig(int phaseIdx) const {
    const auto& p=_phases[phaseIdx];
    GGL::LearnerConfig cfg={};
    static const int64_t SCALED_BATCH[4]={50'000,150'000,300'000,400'000};
    static const int64_t SCALED_MINI_BATCH[4]={10'000,20'000,30'000,40'000};
    const int64_t batch=SCALED_BATCH[RS_CLAMP(phaseIdx,0,3)];
    const int64_t miniBatch=SCALED_MINI_BATCH[RS_CLAMP(phaseIdx,0,3)];
    cfg.numGames=192;
    cfg.tickSkip=8;
    cfg.actionDelay=2;
    cfg.deviceType=GGL::LearnerDeviceType::CPU;
    cfg.checkpointFolder="checkpoints";
    cfg.tsPerSave=5'000'000;
    cfg.checkpointsToKeep=8;
    cfg.standardizeObs=true;
    cfg.standardizeReturns=true;
    cfg.addRewardsToMetrics=true;
    cfg.sendMetrics=true;
    cfg.metricsProjectName="r3maj";
    cfg.metricsGroupName="phases";
    cfg.metricsRunName="R3maJ p1 training";
    cfg.randomSeed=-1;
    cfg.renderMode=false; // R3maJ repo: headless training (Colab/Kaggle). Local WatchPC build overrides to true.

    auto& ppo=cfg.ppo;
    ppo.tsPerItr=batch;
    ppo.batchSize=batch;
    ppo.miniBatchSize=miniBatch;
    ppo.epochs=5; // R3maJ: user-set 5 (was 3)
    ppo.gaeLambda=0.95f;
    ppo.gaeGamma=p.gamma;
    ppo.entropyScale=p.entropyScale;
    ppo.policyLR=p.policyLR;
    ppo.criticLR=p.criticLR;
    ppo.clipRange=0.2f;
    ppo.rewardClipRange=10;
    ppo.policyTemperature=1.f;
    ppo.maxEpisodeDuration=120;

    ppo.sharedHead.layerSizes={512,512};
    ppo.sharedHead.addLayerNorm=true;
    ppo.sharedHead.activationType=GGL::ModelActivationType::RELU;
    ppo.sharedHead.optimType=GGL::ModelOptimType::ADAM;

    ppo.policy.layerSizes={512,512,512,512,512,512};
    ppo.policy.addLayerNorm=true;
    ppo.policy.activationType=GGL::ModelActivationType::RELU;
    ppo.policy.optimType=GGL::ModelOptimType::ADAM;

    ppo.critic.layerSizes={512,512,512,512,512,512};
    ppo.critic.addLayerNorm=true;
    ppo.critic.activationType=GGL::ModelActivationType::RELU;
    ppo.critic.optimType=GGL::ModelOptimType::ADAM;
    return cfg;
}