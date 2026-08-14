#pragma once

#include <RLGymCPP/Rewards/Reward.h>

using namespace RLGC;

// Simple touch reward: fires once on each simulation step where this player
// touches the ball. No height, speed, direction, or mechanic shaping.
class SimpleTouchReward : public Reward {
public:
    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override;
};
