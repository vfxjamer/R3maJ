#pragma once
#include <stdint.h>
#include <vector>
#include <GigaLearnCPP/LearnerConfig.h>

// Timestep after which training episodes may start from real-world replay
// frames or procedural play states instead of a fresh kickoff.
static constexpr int64_t REPLAY_STATE_START = 15'000'000'000;

// Updated every training iteration by main.cpp; consumed by the state setter.
inline int64_t g_trainingTimesteps = 0;

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

	// Phase index 0-3 from total timesteps (0-5B, 5-15B, 15-30B, 30B+)
	int GetCurrentPhase(int64_t totalTimesteps) const;
	const PhaseConfig& GetPhaseConfig(int phaseIdx) const;
	// Effective reward weights for the given total timesteps, including the
	// gradual phase-3 mechanical ramp (mechanical rewards lerp 0 -> Necto target
	// between 30B and 60B).
	PhaseRewards GetRewards(int64_t totalTimesteps) const;
	GGL::LearnerConfig MakeLearnerConfig(int phaseIdx, bool smallModel = false) const;
	int GetNumPhases() const { return 4; }

private:
	PhaseConfig _phases[4];
};
