#pragma once
#include <unordered_map>
#include <vector>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/Reward.h>

using namespace RLGC;

// ============================================================
// DeltaReward â€” wraps any reward to output stepwise deltas
// Stores per-player previous values; returns current - previous
// Eliminates static positional baselines (key Necto insight)
// ============================================================
template<typename T>
class DeltaReward : public Reward {
	T* _inner;
	std::unordered_map<uint32_t, float> _prev;
public:
	DeltaReward(T* inner) : _inner(inner) {}
	virtual ~DeltaReward() { delete _inner; }
	virtual void Reset(const GameState& initialState) override {
		_prev.clear();
		_inner->Reset(initialState);
	}
	virtual void PreStep(const GameState& state) override {
		_inner->PreStep(state);
	}
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		float current = _inner->GetReward(player, state, isFinal);
		auto it = _prev.find(player.carId);
		float prev = (it != _prev.end()) ? it->second : current;
		_prev[player.carId] = current;
		return current - prev;
	}
};

// ============================================================
// GoalDistanceReward (Necto-style exponential decay)
// Returns 0.5 * (exp(-dist_to_opponent_goal / CAR_MAX_SPEED)
//               - exp(-dist_to_own_goal / CAR_MAX_SPEED))
// ============================================================
class GoalDistanceReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// GoalSpeedBonusReward â€” reward for ball speed toward goal on touch
// ============================================================
class GoalSpeedBonusReward : public Reward {
public:
	constexpr static float MAX_REWARDED_SPEED = RLGC::Math::KPHToVel(130);
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// GoalDistBonusReward â€” goal event bonus
//   Scoring team: +based on ball speed
//   Defending team: -based on distance from ball
// (Necto-style goal_dist_bonus_w)
// ============================================================
class GoalDistBonusReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// BoostUsagePenalty (Necto-style boost_lose_w)
// sqrt(clamp) scaling on loss side, only penalizes below GOAL_HEIGHT
// ============================================================
class BoostUsagePenalty : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// PlayerQualityReward â€” per-player positional quality delta
// dist_w * exp(-dist_to_ball / 1410) + align_w * alignment
// Applied as delta each tick (same pattern as GoalDistanceReward)
// ============================================================
class PlayerQualityReward : public Reward {
public:
	PlayerQualityReward(float distW, float alignW) : _distW(distW), _alignW(alignW) {}
	virtual void Reset(const GameState& initialState) override;
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
private:
	float _distW, _alignW;
	std::unordered_map<uint32_t, float> _lastQuality;
	float _ComputeQuality(const Player& player, const GameState& state);
};

// ============================================================
// GroundIdlePenalty (Necto-style touch_grass_w)
// Small penalty for being on ground below ball height
// ============================================================
class GroundIdlePenalty : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// TouchHeightReward â€” Necto-style touch reward
// Higher ball contacts => more reward (with wall proximity bonus)
// Height factor: cube-root activation from dribble height to ceiling
// ============================================================
class TouchHeightReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;

	static float HeightActivation(float z);
	static float DistToClosestWall(float x, float y);
};

// ============================================================
// NectoTouchAccelReward (Necto-style ball speed change on touch)
// Returns ball speed change / CAR_MAX_SPEED, inverse-weighted by touch height
// So ground touches get more accel reward, aerial touches get more height reward
// ============================================================
class NectoTouchAccelReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// WinProbReward â€” win probability delta (Necto-style)
// Tracks score and time remaining, computes P(blue wins) via
// Skellam distribution (Poisson goal model), returns delta
// per step (positive for blue team, negative for orange)
// ============================================================
class WinProbReward : public Reward {
public:
	WinProbReward(int maxSteps, int tickSkip)
		: _maxSteps(maxSteps), _tickSkip(tickSkip) {}

	virtual void Reset(const GameState& initialState) override;
	virtual void PreStep(const GameState& state) override;
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;

private:
	static float _PoissonPMF(float lambda, int k);
	static float _WinProb(int playersPerTeam, float timeLeftSec, int diff);

	int _maxSteps;
	int _tickSkip;
	int _blueScore = 0;
	int _orangeScore = 0;
	int _stepCount = 0;
	float _lastWinProb = 0.5f;
	float _currentDelta = 0.f;
};

// ============================================================
// FlipResetReward â€” Necto/Nexto-style flip reset reward
// Fires when player gains a flip in the air with ball close above
// ============================================================
class FlipResetReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// AngVelReward â€” tiny reward for spinning (Necto-style)
// Prevents policy from collapsing to no-rotation
// ============================================================
class AngVelReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// JumpTouchReward — freestyler reward: touching the ball from
// a jump (early aerial / flip challenge). Fires when the player
// touches the ball while airborne shortly after jumping.
// ============================================================
class JumpTouchReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// WallTouchReward — freestyler reward: ball touches while the
// car is on/near a wall. Uses TouchHeightReward wall-distance
// helper; rewards touches close to any wall surface.
// ============================================================
class WallTouchReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// AirDribbleReward — freestyler reward: escalating reward for
// consecutive sustained air touches with the ball kept close
// (streak counter per car; 0 on first touch, grows each touch).
// ============================================================
class AirDribbleReward : public Reward {
public:
	virtual void Reset(const GameState& initialState) override;
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
private:
	std::unordered_map<uint32_t, int> _streak;
};

// ============================================================
// CradleReward — freestyler reward: carrying the ball on the
// roof/cathedral. Reward per step while the ball rides close
// above the car (roof carry / ceiling-shot setup).
// ============================================================
class CradleReward : public Reward {
public:
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// AllRewardsWrapper â€” aggregates all sub-rewards into one,
// sums them with weights, then applies opponent punishment.
// Used as a single WeightedReward entry in the reward list.
// ============================================================
class AllRewardsWrapper : public Reward {
	std::vector<WeightedReward> _rewards;
	float _opponentPunishW;
public:
	AllRewardsWrapper(const std::vector<WeightedReward>& rewards, float opponentPunishW);
	virtual ~AllRewardsWrapper();

	virtual void Reset(const GameState& initialState) override;
	virtual void PreStep(const GameState& state) override;
	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
	virtual std::vector<float> GetAllRewards(const GameState& state, bool isFinal) override;
};
