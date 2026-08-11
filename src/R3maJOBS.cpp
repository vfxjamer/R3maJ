#include "R3maJOBS.h"

void R3maJOBS::Reset(const GameState& initialState) {
	_blueScore = 0;
	_orangeScore = 0;
	_lastProcessedGoalTick = 0;
}

FList R3maJOBS::BuildObs(const Player& player, const GameState& state) {
	FList obs = {};

	bool inv = (player.team == Team::ORANGE);
	auto invPhys = [&](const PhysState& p) -> PhysState {
		PhysState r = p;
		if (inv) {
			r.pos.x *= -1;
			r.pos.y *= -1;
			r.vel.x *= -1;
			r.vel.y *= -1;
			r.angVel.x *= -1;
			r.angVel.y *= -1;
			r.angVel.z *= -1;
		}
		return r;
	};

	auto ball = invPhys(state.ball);
	obs += ball.pos * POS_COEF;
	obs += ball.vel * VEL_COEF;
	obs += ball.angVel * ANG_VEL_COEF;

	for (int i = 0; i < Action::ELEM_AMOUNT; i++)
		obs += player.prevAction[i];

	auto& pads = state.GetBoostPads(inv);
	auto& padTimers = state.GetBoostPadTimers(inv);
	for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
		if (pads[i]) obs += 1.f;
		else obs += 1.f / (1.f + padTimers[i]);
	}

	auto p = invPhys(player);
	obs += p.pos * POS_COEF;
	obs += p.rotMat.forward;
	obs += p.rotMat.up;
	obs += p.vel * VEL_COEF;
	obs += p.angVel * ANG_VEL_COEF;
	obs += p.rotMat.Dot(p.angVel) * ANG_VEL_COEF;
	obs += p.rotMat.Dot(ball.pos - p.pos) * POS_COEF;
	obs += p.rotMat.Dot(ball.vel - p.vel) * VEL_COEF;
	obs += player.boost / 100.f;
	obs += player.isOnGround ? 1.f : 0.f;
	obs += player.HasFlipOrJump() ? 1.f : 0.f;
	obs += player.isDemoed ? 1.f : 0.f;

	FList teammates, opponents;
	for (auto& other : state.players) {
		if (other.carId == player.carId) continue;
		auto op = invPhys(other);
		auto& target = (other.team == player.team) ? teammates : opponents;
		target += op.pos * POS_COEF;
		target += op.vel * VEL_COEF;
		target += op.angVel * ANG_VEL_COEF;
		target += other.boost / 100.f;
		target += other.isOnGround ? 1.f : 0.f;
		target += other.HasFlipOrJump() ? 1.f : 0.f;
	}

	obs += teammates;
	obs += opponents;

	// Scoreboard tracking
	if (state.goalScored && state.lastTickCount > _lastProcessedGoalTick) {
		_lastProcessedGoalTick = state.lastTickCount;
		Team concede = RS_TEAM_FROM_Y(state.ball.pos.y);
		if (concede == Team::BLUE)
			_orangeScore++;
		else
			_blueScore++;
	}

	int diff = _blueScore - _orangeScore;
	float goalDiff = (float)diff / 5.f;
	goalDiff = RS_CLAMP(goalDiff, -1.f, 1.f);
	obs += goalDiff;

	float ticksElapsed = (float)state.lastTickCount;
	float totalTicks = _maxEpisodeDuration * 120.f;
	float timeLeftSec = RS_MAX(0.f, (totalTicks - ticksElapsed) / 120.f);
	float timeLeftNorm = timeLeftSec / _maxEpisodeDuration;
	obs += timeLeftNorm;

	float isOvertime = (ticksElapsed >= totalTicks) ? 1.f : 0.f;
	obs += isOvertime;

	return obs;
}
