#pragma once

#include <unordered_map>
#include <RLGymCPP/Rewards/Reward.h>
#include <RLGymCPP/Rewards/CommonRewards.h>

using namespace RLGC;

// R3maJ v3 reward system.
// Design principles:
//  - rewards are normalized and small unless they represent a major game event
//  - touch quality is separated from raw contact so touch farming is discouraged
//  - continuous rewards teach fundamentals; event rewards teach outcomes
//  - mechanics are introduced only after the bot has learned to play the game

class TouchReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class SpeedToBallReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class FaceBallReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class AirTimeReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class GroundIdlePenalty : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class BallToGoalReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class TouchAccelerationReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class TouchHeightReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class SaveReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class SaveBoostReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class BoostWastePenalty : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// Rewards meaningful contact with the ball while the car is airborne.
class AirTouchReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// Rewards a flip reset event, not merely being underneath the ball.
class FlipResetReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class WallTouchReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

class AirDribbleReward : public Reward {
public:
    void Reset(const GameState& initialState) override;
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
private:
    std::unordered_map<uint32_t, int> _streak;
};

class CradleReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};

// Tiny rotation encouragement used only in the late curriculum.
class AngularMovementReward : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};
