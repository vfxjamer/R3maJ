#include "R3maJ.h"
#include "PhaseManager.h"
#include "R3maJMetrics.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

using namespace RocketSim;

static PhaseManager g_PhaseManager;
static int g_phaseIdx = 0;
static uint64_t g_totalTimesteps = 0;
static int g_requestedGames = 0;
static Learner* g_learner = nullptr;

static void PrintUsage(const char* progName) {
    RG_LOG("Usage: " << progName << " [options]");
    RG_LOG("  --resume <dir>    Load checkpoint from directory");
    RG_LOG("  --device <type>   Device type: cpu, cuda (default: cpu)");
    RG_LOG("  --games <n>       Number of parallel games (default: 96)");
    RG_LOG("  --save-dir <dir>  Checkpoint save/load directory (default: checkpoints)");
    RG_LOG("  --phase <n>       Force phase index 0-3 (default: auto from checkpoint timesteps)");
    RG_LOG("  --replays <path>  Binary replay file for state initialization");
    RG_LOG("  --run-name <name> W&B run name for this launch");
    RG_LOG("  --new-run         Start a fresh W&B run (do not resume the previous run)");
    RG_LOG("  --carnage         Small network training profile (128x4, 92 games, CPU-only, checkpoints_carnage)");
}

int main(int argc, char* argv[]) {
    SetupSignalHandlers();

    int phaseIdx = -1;
    std::string resumeDir;
    std::string deviceStr = "cpu";
    std::string saveDir = "checkpoints";
    std::string replayPath;
    std::string runName;
    bool newRun = false;
    bool carnage = false;
    int numGames = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "--resume") == 0 && i + 1 < argc) {
            resumeDir = argv[++i];
        } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            deviceStr = argv[++i];
        } else if (strcmp(argv[i], "--games") == 0 && i + 1 < argc) {
            numGames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc) {
            saveDir = argv[++i];
        } else if (strcmp(argv[i], "--phase") == 0 && i + 1 < argc) {
            phaseIdx = atoi(argv[++i]);
            if (phaseIdx >= 0)
                phaseIdx = std::max(0, std::min(phaseIdx, 3));
        } else if (strcmp(argv[i], "--replays") == 0 && i + 1 < argc) {
            replayPath = argv[++i];
        } else if (strcmp(argv[i], "--run-name") == 0 && i + 1 < argc) {
            runName = argv[++i];
        } else if (strcmp(argv[i], "--new-run") == 0) {
            newRun = true;
        } else if (strcmp(argv[i], "--carnage") == 0) {
            carnage = true;
        } else {
            RG_LOG("Unknown option: " << argv[i]);
            PrintUsage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    RocketSim::Init("collision_meshes");

    if (carnage && resumeDir.empty())
        saveDir = "checkpoints_carnage";

    std::string checkpointFolder = !resumeDir.empty() ? resumeDir : saveDir;
    g_totalTimesteps = ReadCheckpointTotalTimesteps(checkpointFolder);

    // W&B run policy: the first launch against a checkpoint folder starts a
    // FRESH run (one time only). Every later launch resumes that same run,
    // so crashes/supervisor restarts never spawn duplicate dashboards.
    // Delete <folder>/.wandb_fresh_done or pass --new-run to force another.
    std::string freshMarker = checkpointFolder + "/.wandb_fresh_done";
    if (!newRun && !std::filesystem::exists(freshMarker)) {
        newRun = true;
        std::ofstream marker(freshMarker);
        marker << "fresh W&B run started" << std::endl;
    }

    if (phaseIdx == -1)
        phaseIdx = g_PhaseManager.GetCurrentPhase(g_totalTimesteps);

    g_phaseIdx = phaseIdx;
    g_rewards = g_PhaseManager.GetRewards(g_totalTimesteps);
    g_replayPath = replayPath;

    LearnerConfig cfg = g_PhaseManager.MakeLearnerConfig(phaseIdx, carnage);
    cfg.checkpointFolder = checkpointFolder;

    if (deviceStr == "cuda")
        cfg.deviceType = GGL::LearnerDeviceType::GPU_CUDA;
    else
        cfg.deviceType = GGL::LearnerDeviceType::CPU;

    if (numGames > 0)
        cfg.numGames = numGames;
    g_requestedGames = cfg.numGames;

    cfg.sendMetrics = true; // R3maJ: native W&B training metrics (policy loss, reward, steps, ...)

    if (!runName.empty())
        cfg.metricsRunName = runName;
    cfg.forceNewRun = newRun;

    RG_LOG(RG_DIVIDER);
    RG_LOG((cfg.renderMode ? (const char*)"=== R3maJ v2 — Renderer ===" : (carnage ? (const char*)"=== R3maJ v2 — Headless Training (CARNAGE profile) ===" : (const char*)"=== R3maJ v2 — Headless Training ===")));
    RG_LOG("  Timesteps at start: " << g_totalTimesteps);
    RG_LOG("  Phase: " << (phaseIdx + 1) << "/4");
    const auto& phaseCfg = g_PhaseManager.GetPhaseConfig(phaseIdx);
    RG_LOG("  Phase range: " << phaseCfg.startStep << " - " << phaseCfg.endStep);
    RG_LOG("  Device: " << (deviceStr == "cuda" ? "CUDA" : "CPU"));
    RG_LOG("  Games: " << (cfg.renderMode ? 1 : cfg.numGames) << (cfg.renderMode ? " (render mode)" : ""));
    RG_LOG("  Tick Skip: " << cfg.tickSkip);
    RG_LOG("  Action Delay: " << cfg.actionDelay);
    if (carnage) {
        RG_LOG("  Network: shared 128x2 -> policy/critic 128x4");
    } else {
        RG_LOG("  Network: shared 512x2 -> policy/critic 512x6");
    }
    RG_LOG("  Epochs: " << cfg.ppo.epochs);
    RG_LOG("  LR: " << cfg.ppo.policyLR);
    RG_LOG("  Gamma: " << phaseCfg.gamma);
    RG_LOG("  Entropy: " << phaseCfg.entropyScale);
    RG_LOG("  MiniBatch: " << cfg.ppo.miniBatchSize);
    RG_LOG("  W&B native metrics: ENABLED");
    RG_LOG("  Checkpoints: " << cfg.checkpointFolder);
    RG_LOG("  Replay source: " << (g_replayPath.empty() ? "default state setter" : g_replayPath));

    RG_LOG("  Rewards: GUIDE STAGE-1 STACK (active in EnvCreateFunc)");
    RG_LOG("    touch=50 (SimpleTouchReward / EventReward touch=1)");
    RG_LOG("    speed_toward_ball=5");
    RG_LOG("    face_ball=1");
    RG_LOG("    air=0.15");
    RG_LOG("    goal/scoring rewards=DISABLED");
    if (g_rewards.opponent_punish_w != 0.f) {
        RG_LOG("    opponent punish (zero-sum)=ON at weight " << g_rewards.opponent_punish_w);
    } else {
        RG_LOG("    opponent punish (zero-sum)=OFF");
    }

    Learner* learner = new Learner(EnvCreateFunc, cfg, StepCallback);
    g_learner = learner;

    learner->iterationCallback = [](GGL::Report& report) {
        g_trainingTimesteps = (int64_t)report["Total Timesteps"];
        AddR3maJMetrics(report, g_activeRewardsWrapper, g_PhaseManager);
    };

    RG_LOG("  Actions: " << learner->numActions);
    RG_LOG("  Obs Size: " << learner->obsSize);
    RG_LOG("  Native W&B telemetry: ENABLED");
    RG_LOG(RG_DIVIDER);

    learner->Start();

    delete learner;
    g_learner = nullptr;
    return EXIT_SUCCESS;
}
