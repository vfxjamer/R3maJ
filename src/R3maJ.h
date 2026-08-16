#pragma once

#include "PhaseManager.h"
#include "R3maJRewards.h"
#include "R3maJStateSetter.h"
#include "R3maJOBS.h"
#include "SimpleTouchReward.h"

#include <GigaLearnCPP/Learner.h>
#include <GigaLearnCPP/Util/Report.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace RocketSim;
using namespace RLGC;
using namespace GGL;

using RewardConfig = PhaseRewards;

inline RewardConfig g_rewards{};
inline std::string g_replayPath;
inline AllRewardsWrapper* g_activeRewardsWrapper = nullptr; // set by EnvCreateFunc

inline void SetupSignalHandlers() {
    // GigaLearn owns the training lifecycle. Keeping this hook explicit makes
    // the entry point compatible with both the headless Colab and local builds.
}

inline uint64_t ReadCheckpointTotalTimesteps(const std::string& folder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root(folder);

    if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
        return 0;

    uint64_t best = 0;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec)
            break;
        if (!entry.is_directory(ec))
            continue;

        const std::string name = entry.path().filename().string();
        if (name.empty() || !std::all_of(name.begin(), name.end(),
            [](unsigned char c) { return c >= '0' && c <= '9'; }))
            continue;

        try {
            best = std::max(best, static_cast<uint64_t>(std::stoull(name)));
        } catch (...) {
            // Ignore directory names that do not fit in uint64_t.
        }
    }

    return best;
}

// Guide-exact AirReward from rewards.md:
// reward 1 while airborne, otherwise 0.
class GuideAirReward final : public Reward {
public:
    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        return player.isOnGround ? 0.f : 1.f;
    }
};

inline EnvCreateResult EnvCreateFunc(int /*index*/) {
    Arena* arena = Arena::Create(GameMode::SOCCAR);
    arena->AddCar(Team::BLUE, CAR_CONFIG_OCTANE);
    arena->AddCar(Team::ORANGE, CAR_CONFIG_OCTANE);

    // ============================================================
    // STAGE 1 — RLGym-PPO Guide reward stack (guide-exact)
    //
    // EventReward(touch=1)          x50
    // SpeedTowardBallReward()       x5
    // FaceBallReward()              x1
    // AirReward()                   x0.15
    //
    // SimpleTouchReward is our EventReward(touch=1) equivalent: fires once
    // per step where the player touches the ball. SpeedTowardBallReward only
    // rewards positive velocity toward the ball (never penalizes moving
    // away). AirReward rewards 1 while airborne, else 0. No zero-sum
    // opponent punish in this stage (guide-exact).
    // ============================================================
    std::vector<WeightedReward> components;
    components.reserve(4);
    components.emplace_back(new SimpleTouchReward(), 50.f);
    components.emplace_back(new SpeedTowardBallReward(), 5.f);
    components.emplace_back(new FaceBallReward(), 1.f);
    components.emplace_back(new GuideAirReward(), 0.15f);

    auto* combined = new AllRewardsWrapper(components, g_rewards.opponent_punish_w);
    g_activeRewardsWrapper = combined;
    std::vector<WeightedReward> rewards = { WeightedReward(combined, 1.f) };

    std::vector<TerminalCondition*> terminals = {
        new GoalScoreCondition(),
        new NoTouchCondition(10)
    };

    auto* obsBuilder = new R3maJOBS(120.f);
    auto* actionParser = new DefaultAction();
    StateSetter* stateSetter = g_replayPath.empty()
        ? static_cast<StateSetter*>(new R3maJStateSetter())
        : static_cast<StateSetter*>(new R3maJStateSetter(g_replayPath));

    return {
        arena,
        rewards,
        terminals,
        obsBuilder,
        actionParser,
        stateSetter,
        nullptr
    };
}

inline void StepCallback(Learner* /*learner*/, const std::vector<GameState>& states, Report& report) {
    if (states.empty())
        return;

    double ballSpeed = 0.0;
    double ballHeight = 0.0;
    double ballAngularSpeed = 0.0;
    double playerSpeed = 0.0;
    double playerBoost = 0.0;
    uint64_t playerCount = 0;
    uint64_t airCount = 0;

    for (const auto& state : states) {
        ballSpeed += state.ball.vel.Length();
        ballHeight += state.ball.pos.z;
        ballAngularSpeed += state.ball.angVel.Length();

        for (const auto& player : state.players) {
            playerSpeed += player.vel.Length();
            playerBoost += player.boost;
            if (!player.isOnGround)
                ++airCount;
            ++playerCount;
        }
    }

    const double invStates = 1.0 / static_cast<double>(states.size());
    report.AddAvg("ball/speed", ballSpeed * invStates);
    report.AddAvg("ball/height", ballHeight * invStates);
    report.AddAvg("ball/angular_speed", ballAngularSpeed * invStates);

    if (playerCount > 0) {
        const double invPlayers = 1.0 / static_cast<double>(playerCount);
        report.AddAvg("player/speed", playerSpeed * invPlayers);
        report.AddAvg("player/boost", playerBoost * invPlayers);
        report.AddAvg("player/air_fraction", static_cast<double>(airCount) * invPlayers);
    }
}
