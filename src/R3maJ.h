#pragma once

#include "PhaseManager.h"
#include "R3maJRewards.h"
#include "R3maJStateSetter.h"
#include "R3maJOBS.h"

#include <GigaLearnCPP/Learner.h>
#include <GigaLearnCPP/Util/Report.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
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

// These are process-local because the executable has one training learner.
inline RewardConfig g_rewards{};
inline std::string g_replayPath;

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

inline EnvCreateResult EnvCreateFunc(int /*index*/) {
    Arena* arena = Arena::Create(GameMode::SOCCAR);
    arena->AddCar(Team::BLUE, CAR_CONFIG_OCTANE);
    arena->AddCar(Team::ORANGE, CAR_CONFIG_OCTANE);

    const auto& r = g_rewards;

    std::vector<WeightedReward> components;
    components.reserve(20);

    components.emplace_back(new GoalReward(-1.f), r.goal_w);
    components.emplace_back(new WinProbReward(120 * 120 / 8, 8), r.win_prob_w);
    components.emplace_back(new GoalDistanceReward(), r.goal_dist_w);
    components.emplace_back(new GoalSpeedBonusReward(), r.goal_speed_bonus_w);
    components.emplace_back(new GoalDistBonusReward(), r.goal_dist_bonus_w);
    components.emplace_back(new TouchHeightReward(), r.touch_height_w);
    components.emplace_back(new NectoTouchAccelReward(), r.touch_accel_w);
    components.emplace_back(new PickupBoostReward(), r.boost_gain_w);
    components.emplace_back(new BoostUsagePenalty(), r.boost_lose_w);
    components.emplace_back(new DemoReward(), r.demo_w);
    components.emplace_back(new FlipResetReward(), r.flip_reset_w);
    components.emplace_back(new JumpTouchReward(), r.jump_touch_w);
    components.emplace_back(new WallTouchReward(), r.wall_touch_w);
    components.emplace_back(new AirDribbleReward(), r.air_dribble_w);
    components.emplace_back(new CradleReward(), r.cradle_w);
    components.emplace_back(new AngVelReward(), r.ang_vel_w);
    components.emplace_back(new GroundIdlePenalty(), r.touch_grass_w);
    components.emplace_back(new PlayerQualityReward(r.dist_w, r.align_w), 1.f);

    auto* combined = new AllRewardsWrapper(components, r.opponent_punish_w);
    std::vector<WeightedReward> rewards = { WeightedReward(combined, 1.f) };

    std::vector<TerminalCondition*> terminals = {
        new GoalScoreCondition(),
        new NoTouchCondition(120 * 120 / 8)
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
    uint64_t playerCount = 0;

    for (const auto& state : states) {
        ballSpeed += state.ball.vel.Length();
        ballHeight += state.ball.pos.z;
        ballAngularSpeed += state.ball.angVel.Length();

        for (const auto& player : state.players) {
            playerSpeed += player.vel.Length();
            ++playerCount;
        }
    }

    const double invStates = 1.0 / static_cast<double>(states.size());
    report.AddAvg("ball/speed", ballSpeed * invStates);
    report.AddAvg("ball/height", ballHeight * invStates);
    report.AddAvg("ball/angular_speed", ballAngularSpeed * invStates);

    if (playerCount > 0)
        report.AddAvg("player/speed", playerSpeed / static_cast<double>(playerCount));
}
