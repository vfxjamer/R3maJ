#pragma once
#include <stdint.h>
#include <GigaLearnCPP/LearnerConfig.h>

struct PhaseRewards {
    float touch_w;
    float speed_to_ball_w;
    float face_ball_w;
    float air_time_w;
    float ground_idle_w;
    float ball_to_goal_w;
    float touch_accel_w;
    float touch_height_w;
    float goal_w;
    float save_w;
    float boost_gain_w;
    float save_boost_w;
    float boost_waste_w;
    float demo_w;
    float air_touch_w;
    float flip_reset_w;
    float wall_touch_w;
    float air_dribble_w;
    float cradle_w;
    float angular_movement_w;
};

struct PhaseConfig {
    int64_t startStep;
    int64_t endStep;
    float gamma;
    float entropyScale;
    float policyLR;
    float criticLR;
    PhaseRewards rewards;
};

class PhaseManager {
public:
    PhaseManager();
    int GetCurrentPhase(int64_t totalTimesteps) const;
    const PhaseConfig& GetPhaseConfig(int phaseIdx) const;
    PhaseRewards GetRewards(int64_t totalTimesteps) const;
    GGL::LearnerConfig MakeLearnerConfig(int phaseIdx) const;
    int GetNumPhases() const { return 4; }
private:
    PhaseConfig _phases[4];
};
