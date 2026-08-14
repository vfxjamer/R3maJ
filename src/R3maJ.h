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
inline RewardConfig g_rewards{};
inline std::string g_replayPath;

inline void SetupSignalHandlers() {}

inline uint64_t ReadCheckpointTotalTimesteps(const std::string& folder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path root(folder);
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) return 0;
    uint64_t best = 0;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (ec || !e.is_directory(ec)) continue;
        std::string n = e.path().filename().string();
        if (n.empty() || !std::all_of(n.begin(), n.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) continue;
        try { best = std::max(best, static_cast<uint64_t>(std::stoull(n))); } catch (...) {}
    }
    return best;
}

inline EnvCreateResult EnvCreateFunc(int) {
    Arena* arena = Arena::Create(GameMode::SOCCAR);
    arena->AddCar(Team::BLUE, CAR_CONFIG_OCTANE);
    arena->AddCar(Team::ORANGE, CAR_CONFIG_OCTANE);

    const auto& r = g_rewards;
    std::vector<WeightedReward> rewards;
    rewards.reserve(19);

    rewards.emplace_back(new TouchReward(), r.touch_w);
    rewards.emplace_back(new SpeedToBallReward(), r.speed_to_ball_w);
    rewards.emplace_back(new FaceBallReward(), r.face_ball_w);
    rewards.emplace_back(new AirTimeReward(), r.air_time_w);
    rewards.emplace_back(new GroundIdlePenalty(), r.ground_idle_w);
    rewards.emplace_back(new BallToGoalReward(), r.ball_to_goal_w);
    rewards.emplace_back(new TouchAccelerationReward(), r.touch_accel_w);
    rewards.emplace_back(new TouchHeightReward(), r.touch_height_w);

    // Guide aggression bias ~= 0.20:
    // score = +20, concede = -20 * (1 - 0.20) = -16.
    rewards.emplace_back(new GoalReward(-0.8f), r.goal_w);

    rewards.emplace_back(new SaveReward(), r.save_w);

    // PickupBoostReward uses sqrt(boost_delta), which naturally gives small
    // pads substantially more than their raw 12% boost fraction, as recommended.
    rewards.emplace_back(new PickupBoostReward(), r.boost_gain_w);
    rewards.emplace_back(new SaveBoostReward(), r.save_boost_w);
    rewards.emplace_back(new DemoReward(), r.demo_w);
    rewards.emplace_back(new AirTouchReward(), r.air_touch_w);
    rewards.emplace_back(new FlipResetReward(), r.flip_reset_w);
    rewards.emplace_back(new WallTouchReward(), r.wall_touch_w);
    rewards.emplace_back(new AirDribbleReward(), r.air_dribble_w);
    rewards.emplace_back(new CradleReward(), r.cradle_w);
    rewards.emplace_back(new AngularMovementReward(), r.angular_movement_w);

    std::vector<TerminalCondition*> terminals = {
        new GoalScoreCondition(),
        new NoTouchCondition(120 * 120 / 8)
    };

    auto* obsBuilder = new R3maJOBS(120.f);
    auto* actionParser = new DefaultAction();
    StateSetter* setter = g_replayPath.empty()
        ? static_cast<StateSetter*>(new R3maJStateSetter())
        : static_cast<StateSetter*>(new R3maJStateSetter(g_replayPath));

    return {arena, rewards, terminals, obsBuilder, actionParser, setter, nullptr};
}

inline void StepCallback(Learner*, const std::vector<GameState>& states, Report& report) {
    if (states.empty()) return;
    double bs = 0, bh = 0, ba = 0, ps = 0;
    uint64_t pc = 0;
    for (const auto& s : states) {
        bs += s.ball.vel.Length();
        bh += s.ball.pos.z;
        ba += s.ball.angVel.Length();
        for (const auto& p : s.players) {
            ps += p.vel.Length();
            ++pc;
        }
    }
    double inv = 1.0 / states.size();
    report.AddAvg("ball/speed", bs * inv);
    report.AddAvg("ball/height", bh * inv);
    report.AddAvg("ball/angular_speed", ba * inv);
    if (pc) report.AddAvg("player/speed", ps / static_cast<double>(pc));
}
