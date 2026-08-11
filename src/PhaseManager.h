#pragma once
#include <stdint.h>
#include <vector>
#include <GigaLearnCPP/LearnerConfig.h>

struct PhaseRewards {
	float boost_gain_w;
	float boost_lose_w;
	float ang_vel_w;
	float touch_grass_w;
	float goal_w;
	float win_prob_w;
	float goal_dist_w;
	float goal_speed_bonus_w;
	float touch_height_w;
	float touch_accel_w;
	float flip_reset_w;
	float demo_w;
	float opponent_punish_w;
	float goal_dist_bonus_w;
	float dist_w;
	float align_w;
	float jump_touch_w;
	float wall_touch_w;
	float air_dribble_w;
	float cradle_w;
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
	GGL::LearnerConfig MakeLearnerConfig(int phaseIdx) const;
	int GetNumPhases() const { return 4; }

private:
	PhaseConfig _phases[4];
};
