#include "R3maJRewards.h"
#include <cmath>

// ---- GoalDistanceReward (exponential, Necto-style) ----
float GoalDistanceReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	Vec targetPos = (player.team == Team::BLUE)
		? CommonValues::ORANGE_GOAL_BACK
		: CommonValues::BLUE_GOAL_BACK;
	Vec ownPos = (player.team == Team::BLUE)
		? CommonValues::BLUE_GOAL_BACK
		: CommonValues::ORANGE_GOAL_BACK;

	float distToTarget = (targetPos - state.ball.pos).Length();
	float distToOwn = (ownPos - state.ball.pos).Length();

	float goalExp = expf(-distToTarget / CommonValues::CAR_MAX_SPEED);
	float ownExp = expf(-distToOwn / CommonValues::CAR_MAX_SPEED);

	// Range: -0.5 (ball at opponent net) to +0.5 (ball at own net)
	// Wrapped in DeltaReward to only fire on CHANGE
	return 0.5f * (goalExp - ownExp);
}

// ---- GoalSpeedBonusReward (Necto-style, fires only on goal events) ----
float GoalSpeedBonusReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!state.goalScored) return 0;

	float speed = state.ball.vel.Length();
	return RS_MIN(1.f, speed / CommonValues::BALL_MAX_SPEED);
}

// ---- GoalDistBonusReward ----
float GoalDistBonusReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!state.goalScored || !state.prev) return 0;

	bool playerScored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));

	if (playerScored) {
		float speed = state.ball.vel.Length();
		return RS_MIN(1.f, speed / CommonValues::BALL_MAX_SPEED);
	} else {
		float distToBall = (player.pos - state.prev->ball.pos).Length();
		return -(1.f - expf(-distToBall / CommonValues::CAR_MAX_SPEED));
	}
}

// ---- BoostUsagePenalty (Necto-style, sqrt scaling) ----
float BoostUsagePenalty::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!player.prev) return 0;
	float sqrtCurr = sqrtf(RS_CLAMP(player.boost / 100.f, 0.f, 1.f));
	float sqrtPrev = sqrtf(RS_CLAMP(player.prev->boost / 100.f, 0.f, 1.f));
	float diff = sqrtCurr - sqrtPrev;
	if (diff >= 0) return 0;                                      // gain handled by PickupBoostReward
	if (player.pos.z >= CommonValues::GOAL_HEIGHT) return 0;     // free aerials above crossbar
	float heightScale = 1.f - player.pos.z / CommonValues::GOAL_HEIGHT;
	return diff * heightScale;
}

// ---- PlayerQualityReward ----
float PlayerQualityReward::_ComputeQuality(const Player& player, const GameState& state) {
	Vec ballDir = (state.ball.pos - player.pos).Normalized();
	float dist  = (state.ball.pos - player.pos).Length();
	float liuDist = expf(-dist / 1410.f);

	Vec toOppGoal = ((player.team == Team::BLUE)
		? CommonValues::ORANGE_GOAL_BACK
		: CommonValues::BLUE_GOAL_BACK) - player.pos;
	Vec toOwnGoal = ((player.team == Team::BLUE)
		? CommonValues::BLUE_GOAL_BACK
		: CommonValues::ORANGE_GOAL_BACK) - player.pos;

	float alignOpp = ballDir.Dot(toOppGoal.Normalized());
	float alignOwn = ballDir.Dot(toOwnGoal.Normalized());
	float alignment = 0.5f * (alignOpp - alignOwn);

	return _distW * liuDist + _alignW * alignment;
}

void PlayerQualityReward::Reset(const GameState& initialState) {
	_lastQuality.clear();
	for (const auto& player : initialState.players)
		_lastQuality[player.carId] = _ComputeQuality(player, initialState);
}

float PlayerQualityReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	float q = _ComputeQuality(player, state);
	float delta = q - _lastQuality[player.carId];
	_lastQuality[player.carId] = q;
	return delta;
}

// ---- GroundIdlePenalty (Necto-style, tiny) ----
float GroundIdlePenalty::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (player.isOnGround && player.pos.z < CommonValues::BALL_RADIUS) {
		return -1.f;
	}
	return 0;
}

// ---- TouchHeightReward ----
float TouchHeightReward::HeightActivation(float z) {
	return cbrtf((z - 150.f) / CommonValues::CEILING_Z);
}

float TouchHeightReward::DistToClosestWall(float x, float y) {
	float absX = fabsf(x);
	float absY = fabsf(y);

	float distSideWall = CommonValues::SIDE_WALL_X - absX;
	float distBackWall = CommonValues::BACK_WALL_Y - absY;

	// Distance to corner wall segment
	// From (SIDE_WALL_X - 1152, BACK_WALL_Y) to (SIDE_WALL_X, BACK_WALL_Y - 1152)
	constexpr float CORNER_INSET = 1152.f;
	float x1 = CommonValues::SIDE_WALL_X - CORNER_INSET;
	float y1 = CommonValues::BACK_WALL_Y;
	float x2 = CommonValues::SIDE_WALL_X;
	float y2 = CommonValues::BACK_WALL_Y - CORNER_INSET;

	float A = absX - x1;
	float B = absY - y1;
	float C = x2 - x1;
	float D = y2 - y1;

	float dot = A * C + B * D;
	float lenSq = C * C + D * D;
	float param = (lenSq != 0) ? dot / lenSq : -1.f;

	float xx, yy;
	if (param < 0) {
		xx = x1; yy = y1;
	} else if (param > 1) {
		xx = x2; yy = y2;
	} else {
		xx = x1 + param * C;
		yy = y1 + param * D;
	}

	float dx = absX - xx;
	float dy = absY - yy;
	float distCorner = sqrtf(dx * dx + dy * dy);

	return fminf(fminf(distSideWall, distBackWall), distCorner);
}

float TouchHeightReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!state.prev) return 0;
	if (!player.ballTouchedStep) return 0;

	float carHeight = player.pos.z;
	float ballHeight = state.ball.pos.z;
	float avgHeight = 0.5f * (carHeight + ballHeight);

	float h0 = HeightActivation(0);
	float h1 = HeightActivation(CommonValues::CEILING_Z);
	float hx = HeightActivation(avgHeight);
	float heightFactor = (hx - h0) / (h1 - h0);
	heightFactor *= heightFactor;

	float wallDist = DistToClosestWall(player.pos.x, player.pos.y);
	float wallDistFactor = 1.f - expf(-wallDist / CommonValues::CAR_MAX_SPEED);

	return heightFactor * (1.f + wallDistFactor);
}

// ---- NectoTouchAccelReward (Necto-style, inverse height weighting) ----
float NectoTouchAccelReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!state.prev) return 0;
	if (!player.ballTouchedStep) return 0;

	float carHeight = player.pos.z;
	float ballHeight = state.ball.pos.z;
	float avgHeight = 0.5f * (carHeight + ballHeight);

	float h0 = TouchHeightReward::HeightActivation(0);
	float h1 = TouchHeightReward::HeightActivation(CommonValues::CEILING_Z);
	float hx = TouchHeightReward::HeightActivation(avgHeight);
	float heightFactor = (hx - h0) / (h1 - h0);
	heightFactor *= heightFactor;

	Vec velDelta = state.ball.vel - state.prev->ball.vel;
	float speedChange = velDelta.Length();

	// Inverse height weighting: ground touches get more accel reward
	return (1.f - heightFactor) * (speedChange / CommonValues::CAR_MAX_SPEED);
}

// ---- WinProbReward ----
// Goals per minute by team size (from rocket_learn / ballchasing stats)
static constexpr float GOALS_PER_MIN_TABLE[] = { 1.0f, 0.6f, 0.45f };

// Floor area used for goal-floor ratio (from rocket_learn)
static constexpr float FLOOR_AREA = 4.f * 5120.f * 4096.f - 1152.f * 1152.f;
static constexpr float GOAL_AREA = 850.f * 880.f; // GOAL_HEIGHT (~850) * 880
static constexpr float GOAL_FLOOR_RATIO = GOAL_AREA / (2.f * GOAL_AREA + FLOOR_AREA);

float WinProbReward::_PoissonPMF(float lambda, int k) {
	float logP = -lambda + (float)k * logf(lambda);
	for (int i = 2; i <= k; i++)
		logP -= logf((float)i);
	return expf(logP);
}

float WinProbReward::_WinProb(int playersPerTeam, float timeLeftSec, int diff) {
	int idx = RS_CLAMP(playersPerTeam - 1, 0, 2);
	float gpm = GOALS_PER_MIN_TABLE[idx];
	float mu = gpm * timeLeftSec / 60.f;

	if (timeLeftSec <= 0.f) {
		if (diff >= 2) return 1.f;
		if (diff == 1) return 1.f - GOAL_FLOOR_RATIO * 0.5f;
		if (diff == 0) return 0.5f;
		if (diff == -1) return GOAL_FLOOR_RATIO * 0.5f;
		return 0.f;
	}

	const int MAX_GOALS = 15;
	float probBlue = 0.f;

	for (int b = 0; b <= MAX_GOALS; b++) {
		float pb = _PoissonPMF(mu, b);
		if (pb < 1e-10f) continue;
		for (int o = 0; o <= MAX_GOALS; o++) {
			float po = _PoissonPMF(mu, o);
			if (po < 1e-10f) continue;
			float joint = pb * po;
			int finalDiff = diff + (b - o);
			if (finalDiff > 0) {
				probBlue += joint;
			} else if (finalDiff == 0) {
				probBlue += joint * 0.5f;
			}
		}
	}

	return probBlue;
}

void WinProbReward::Reset(const GameState& initialState) {
	_blueScore = 0;
	_orangeScore = 0;
	_stepCount = 0;
	_lastWinProb = _WinProb(
		(int)initialState.players.size() / 2,
		(float)_maxSteps * _tickSkip / 120.f,
		0
	);
	_currentDelta = 0.f;
}

void WinProbReward::PreStep(const GameState& state) {
	if (state.prev && state.goalScored) {
		if (state.ball.pos.y > 0) {
			_blueScore++; // Ball in orange side â†’ blue scored
		} else {
			_orangeScore++; // Ball in blue side â†’ orange scored
		}
	}

	int diff = _blueScore - _orangeScore;
	float ticksLeft = (float)(_maxSteps - _stepCount) * _tickSkip;
	float timeLeftSec = ticksLeft / 120.f;
	int playersPerTeam = (int)state.players.size() / 2;

	float winProb = _WinProb(playersPerTeam, timeLeftSec, diff);

	_currentDelta = winProb - _lastWinProb;
	_lastWinProb = winProb;
	_stepCount++;
}

float WinProbReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	bool isBlue = (player.team == Team::BLUE);
	return isBlue ? _currentDelta : -_currentDelta;
}

// ---- FlipResetReward (Necto/Nexto-style) ----
float FlipResetReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!player.prev) return 0;

	bool hasFlip = player.HasFlipOrJump();
	bool hadFlip = player.prev->HasFlipOrJump();
	if (!hasFlip || hadFlip) return 0;

	if (player.pos.z <= 3.f * CommonValues::BALL_RADIUS) return 0;

	Vec ballRel = state.ball.pos - player.pos;
	float dist = ballRel.Length();
	if (dist >= 2.f * CommonValues::BALL_RADIUS) return 0;

	float cosAngle = ballRel.Normalized().Dot(-player.rotMat.up);
	if (cosAngle <= 0.9f) return 0;

	return 1.f;
}

// ---- AngVelReward ----
float AngVelReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	float angVelNorm = player.angVel.Length() / CommonValues::CAR_MAX_ANG_VEL;
	return angVelNorm;
}

// ---- JumpTouchReward ----
// Touching the ball while airborne shortly after a jump (early aerial / challenge)
float JumpTouchReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!player.ballTouchedStep) return 0;
	if (player.isOnGround) return 0;

	// Only credit jumps-aerial touches: left ground via jump and not a prolonged float
	if (!player.hasJumped) return 0;
	if (player.airTimeSinceJump > 2.5f) return 0;

	float carHeight = player.pos.z;
	float ballHeight = state.ball.pos.z;
	float heightFactor = (0.5f * (carHeight + ballHeight) - 150.f) / CommonValues::CEILING_Z;
	heightFactor = RS_CLAMP(heightFactor, 0.f, 1.f);

	// A bit more reward than a flat touch: base + height scaling (up to 1.5)
	return 1.f + 0.5f * heightFactor;
}

// ---- WallTouchReward ----
float WallTouchReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!player.ballTouchedStep) return 0;

	float wallDist = TouchHeightReward::DistToClosestWall(player.pos.x, player.pos.y);

	// Near a wall surface (car on wall or hugging it)
	if (wallDist > 400.f) return 0;

	float ballHeight = state.ball.pos.z;
	float heightFactor = (ballHeight - 150.f) / CommonValues::CEILING_Z;
	heightFactor = RS_CLAMP(heightFactor, 0.f, 1.f);

	return 1.f + heightFactor;
}

// ---- AirDribbleReward ----
void AirDribbleReward::Reset(const GameState& initialState) {
	_streak.clear();
}

float AirDribbleReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (!player.ballTouchedStep) {
		if (!player.isOnGround)
			_streak[player.carId] = 0; // Airborne without touch breaks the dribble
		return 0;
	}

	// Ball must be close to the car nose to count as a controlled air touch
	float dist = (state.ball.pos - player.pos).Length();
	if (dist > 1.75f * CommonValues::BALL_RADIUS) {
		_streak[player.carId] = 0;
		return 0;
	}

	int& streak = _streak[player.carId];
	streak++;
	if (streak < 2) return 0; // First touch is a setup

	float r = (float)(streak - 1);
	// Escalating but capped: 1, 2, 3, then 3
	if (r > 3.f) r = 3.f;
	return r;
}

// ---- CradleReward ----
float CradleReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
	if (player.isOnGround) return 0;

	Vec ballRel = state.ball.pos - player.pos;

	// Ball riding above/near the car roof
	if (ballRel.z < 0.f) return 0;
	float horizDist = sqrtf(ballRel.x * ballRel.x + ballRel.y * ballRel.y);
	if (horizDist > CommonValues::BALL_RADIUS * 1.25f) return 0;
	if (ballRel.z > CommonValues::BALL_RADIUS * 2.2f) return 0;

	return 1.f;
}

// ---- AllRewardsWrapper ----
AllRewardsWrapper::AllRewardsWrapper(const std::vector<WeightedReward>& rewards, float opponentPunishW)
	: _rewards(rewards), _opponentPunishW(opponentPunishW) {}

AllRewardsWrapper::~AllRewardsWrapper() {
	for (auto& wr : _rewards)
		delete wr.reward;
}

void AllRewardsWrapper::Reset(const GameState& initialState) {
	for (auto& wr : _rewards)
		wr.reward->Reset(initialState);
}

void AllRewardsWrapper::PreStep(const GameState& state) {
	for (auto& wr : _rewards)
		wr.reward->PreStep(state);
}

float AllRewardsWrapper::GetReward(const Player& player, const GameState& state, bool isFinal) {
	float total = 0;
	for (auto& wr : _rewards)
		total += wr.reward->GetReward(player, state, isFinal) * wr.weight;
	return total;
}

std::vector<float> AllRewardsWrapper::GetAllRewards(const GameState& state, bool isFinal) {
	std::vector<float> rewards(state.players.size(), 0);

	// Sum all sub-rewards
	for (auto& wr : _rewards) {
		auto output = wr.reward->GetAllRewards(state, isFinal);
		for (int i = 0; i < (int)state.players.size(); i++)
			rewards[i] += output[i] * wr.weight;
	}

	// Apply opponent punish
	if (_opponentPunishW != 0) {
		float teamAvgs[2] = {};
		int teamCounts[2] = {};
		for (int i = 0; i < (int)state.players.size(); i++) {
			int t = (int)state.players[i].team;
			teamCounts[t]++;
			teamAvgs[t] += rewards[i];
		}
		if (teamCounts[0] > 0) teamAvgs[0] /= teamCounts[0];
		if (teamCounts[1] > 0) teamAvgs[1] /= teamCounts[1];

		for (int i = 0; i < (int)state.players.size(); i++) {
			int t = (int)state.players[i].team;
			rewards[i] -= _opponentPunishW * teamAvgs[1 - t];
		}
	}

	return rewards;
}
