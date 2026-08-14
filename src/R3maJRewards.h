#pragma once
#include <unordered_map>
#include <vector>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/Reward.h>

using namespace RLGC;

// Exact early-stage guide helper: reward velocity toward the ball.
// This is the C++ equivalent of the guide's SpeedTowardBallReward().
class SpeedTowardBallReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// ============================================================
// DeltaReward — wraps any reward to output stepwise deltas
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
    virtual void PreStep(const GameState& state) override { _inner->PreStep(state); }
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float current = _inner->GetReward(player, state, isFinal);
        auto it = _prev.find(player.carId);
        float prev = (it != _prev.end()) ? it->second : current;
        _prev[player.carId] = current;
        return current - prev;
    }
};

class GoalDistanceReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class GoalSpeedBonusReward : public Reward {
public:
    constexpr static float MAX_REWARDED_SPEED = RLGC::Math::KPHToVel(130);
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class GoalDistBonusReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class BoostUsagePenalty : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

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

class GroundIdlePenalty : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class TouchHeightReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
    static float HeightActivation(float z);
    static float DistToClosestWall(float x, float y);
};

class NectoTouchAccelReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class WinProbReward : public Reward {
public:
    WinProbReward(int maxSteps, int tickSkip) : _maxSteps(maxSteps), _tickSkip(tickSkip) {}
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

class FlipResetReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class AngVelReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class JumpTouchReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class WallTouchReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class AirDribbleReward : public Reward {
public:
    virtual void Reset(const GameState& initialState) override;
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
private:
    std::unordered_map<uint32_t, int> _streak;
};

class CradleReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

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
