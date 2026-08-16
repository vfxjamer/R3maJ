#pragma once

#include <string>
#include <vector>

#include <GigaLearnCPP/Util/Report.h>

#include "PhaseManager.h"
#include "R3maJRewards.h"

static const char* const kComponentNames[7] = {
    "touch", "goal", "accel", "vtg", "speed", "face", "air"
};

// Adds R3maJ-specific metrics (per-component reward averages, goal rates,
// phase progress) to the per-iteration report. Called from the learner's
// iterationCallback so the values flow to W&B and the console report.
inline void AddR3maJMetrics(GGL::Report& report, AllRewardsWrapper* wrapper, const PhaseManager& phaseMgr) {
    if (!wrapper)
        return;

    auto m = wrapper->PopMetrics();
    if (m.steps <= 0)
        return;

    for (size_t i = 0; i < m.components.size() && i < 7; i++)
        report[(std::string("Reward/") + kComponentNames[i]).c_str()] =
            m.components[i] / (double)m.steps;

    double epLen = report.Has("Episode Length") ? report["Episode Length"] : 200.0;
    double games = m.steps / epLen;
    if (games > 0) {
        report["Goals For / 100 games"] = m.goalsFor / games * 100.0;
        report["Goals Conceded / 100 games"] = m.goalsConceded / games * 100.0;
        report["Net Goals / 100 games"] = (m.goalsFor - m.goalsConceded) / games * 100.0;
    }

    int64_t ts = (int64_t)report["Total Timesteps"];
    int phase = phaseMgr.GetCurrentPhase(ts);
    const auto& pc = phaseMgr.GetPhaseConfig(phase);
    double prog = (double)(ts - pc.startStep) / (double)(pc.endStep - pc.startStep) * 100.0;
    report["Phase"] = phase;
    report["Phase Progress %"] = prog < 0 ? 0.0 : (prog > 100.0 ? 100.0 : prog);
}