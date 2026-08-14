#include "SimpleTouchReward.h"

float SimpleTouchReward::GetReward(const Player& player, const GameState& state, bool isFinal) {
    return player.ballTouchedStep ? 1.f : 0.f;
}
