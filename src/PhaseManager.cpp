#include "PhaseManager.h"
#include <GigaLearnCPP/PPO/PPOLearnerConfig.h>
#include <GigaLearnCPP/Util/ModelConfig.h>

PhaseManager::PhaseManager() {
	PhaseRewards nectoRewards = {
		.boost_gain_w = 1.5f,
		.boost_lose_w = 0.8f,
		.ang_vel_w = 0.005f,
		.touch_grass_w = 0.005f,
		.goal_w = 10.f,
		.win_prob_w = 10.f,
		.goal_dist_w = 10.f,
		.goal_speed_bonus_w = 2.5f,
		.touch_height_w = 3.f,
		.touch_accel_w = 0.5f,
		.flip_reset_w = 10.f,
		.demo_w = 8.f,
		.opponent_punish_w = 1.f,
		.goal_dist_bonus_w = 2.5f,
		.dist_w = 0.25f,
		.align_w = 0.25f,
		.jump_touch_w = 6.f,
		.wall_touch_w = 2.f,
		.air_dribble_w = 2.f,
		.cradle_w = 1.f,
	};

	_phases[0] = { 0, 30'000'000'000, 0.990f, 0.05f, 1e-4f, 1e-4f, nectoRewards };
	_phases[1] = { 30'000'000'000, 80'000'000'000, 0.993f, 0.03f, 1e-4f, 1e-4f, nectoRewards };
	_phases[2] = { 80'000'000'000, 150'000'000'000, 0.995f, 0.02f, 1e-4f, 1e-4f, nectoRewards };
	_phases[3] = { 150'000'000'000, 200'000'000'000, 0.998f, 0.01f, 1e-4f, 1e-4f, nectoRewards };
}

int PhaseManager::GetCurrentPhase(int64_t totalTimesteps) const {
	for (int i = 0; i < 4; i++) {
		if (totalTimesteps >= _phases[i].startStep && totalTimesteps < _phases[i].endStep)
			return i;
	}
	return 3;
}

const PhaseConfig& PhaseManager::GetPhaseConfig(int phaseIdx) const {
	return _phases[phaseIdx];
}

GGL::LearnerConfig PhaseManager::MakeLearnerConfig(int phaseIdx) const {
	const auto& p = _phases[phaseIdx];
	GGL::LearnerConfig cfg = {};

	// Batch scaling: 100k -> 200k -> 300k -> 400k (phase 0..3)
	static const int64_t SCALED_BATCH[4] = { 100'000, 200'000, 300'000, 400'000 };
	const int64_t batch = SCALED_BATCH[RS_CLAMP(phaseIdx, 0, 3)];

	cfg.numGames = 96;
	cfg.tickSkip = 8;
	cfg.actionDelay = 2;
	cfg.deviceType = GGL::LearnerDeviceType::CPU;
	cfg.checkpointFolder = "checkpoints";
	cfg.tsPerSave = 5'000'000;
	cfg.checkpointsToKeep = 8;
	cfg.standardizeObs = true;
	cfg.standardizeReturns = true;
	cfg.addRewardsToMetrics = true;
	cfg.sendMetrics = true;
	cfg.metricsProjectName = "r3maj";
	cfg.metricsGroupName = "phases";
	cfg.metricsRunName = "R3maJ p1 training";
	cfg.randomSeed = -1;
	cfg.renderMode = false;

	auto& ppo = cfg.ppo;
	ppo.tsPerItr = batch;
	ppo.batchSize = batch;
	ppo.miniBatchSize = batch / 10;
	ppo.epochs = 5;
	ppo.gaeLambda = 0.99f;
	ppo.gaeGamma = p.gamma;
	ppo.entropyScale = p.entropyScale;
	ppo.policyLR = p.policyLR;
	ppo.criticLR = p.criticLR;
	ppo.clipRange = 0.2f;
	ppo.rewardClipRange = 10;
	ppo.policyTemperature = 1.f;
	ppo.maxEpisodeDuration = 120;

	ppo.sharedHead.layerSizes = { 512, 512 };
	ppo.sharedHead.addLayerNorm = true;
	ppo.sharedHead.activationType = GGL::ModelActivationType::RELU;
	ppo.sharedHead.optimType = GGL::ModelOptimType::ADAM;

	ppo.policy.layerSizes = { 512, 512, 512, 512, 512, 512 };
	ppo.policy.addLayerNorm = true;
	ppo.policy.activationType = GGL::ModelActivationType::RELU;
	ppo.policy.optimType = GGL::ModelOptimType::ADAM;

	ppo.critic.layerSizes = { 512, 512, 512, 512, 512, 512 };
	ppo.critic.addLayerNorm = true;
	ppo.critic.activationType = GGL::ModelActivationType::RELU;
	ppo.critic.optimType = GGL::ModelOptimType::ADAM;

	return cfg;
}
