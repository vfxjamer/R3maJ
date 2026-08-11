#pragma once
#include <RLGymCPP/ObsBuilders/ObsBuilder.h>

using namespace RLGC;

class R3maJOBS : public ObsBuilder {
public:
	constexpr static float
		POS_COEF = 1.f / 5000.f,
		VEL_COEF = 1.f / 2300.f,
		ANG_VEL_COEF = 1.f / 3.f;

	R3maJOBS(float maxEpisodeDuration = 120.f) : _maxEpisodeDuration(maxEpisodeDuration) {}

	virtual void Reset(const GameState& initialState) override;
	virtual FList BuildObs(const Player& player, const GameState& state) override;

private:
	float _maxEpisodeDuration;
	int _blueScore = 0;
	int _orangeScore = 0;
	uint64_t _lastProcessedGoalTick = 0;
};
