#include <csignal>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <GigaLearnCPP/Learner.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include "R3maJOBS.h"
#include "R3maJRewards.h"
#include "R3maJStateSetter.h"
#include "PhaseManager.h"

using namespace GGL;
using namespace RLGC;

static PhaseManager g_PhaseManager;
static std::atomic<bool> g_quitRequested(false);
static GGL::Learner* g_learner = nullptr;
static std::string g_replayPath;

// Reward weights for the current run, resolved from totalTimesteps before the
// Learner is constructed (envs are built before the checkpoint stats are loaded).
static PhaseRewards g_rewards;
static int64_t g_totalTimesteps = 0;

void SignalHandler(int) {
	g_quitRequested = true;
}

void SetupSignalHandlers() {
	std::signal(SIGINT, SignalHandler);
	std::signal(SIGTERM, SignalHandler);
}

// R3maJ: keypress detector disabled - no interactive key reads
char GetPressedCharNoBlock() {
	return 0;
}

// Minimal RUNNING_STATS.json parser: extracts the "total_timesteps" field.
// (Kept dependency-free so main.cpp doesn't need nlohmann on its include path.)
static int64_t FindTotalTimestepsInJson(const std::string& filePath) {
	std::ifstream fIn(filePath);
	if (!fIn.good())
		return -1;
	std::string text((std::istreambuf_iterator<char>(fIn)), std::istreambuf_iterator<char>());

	const std::string key = "\"total_timesteps\"";
	size_t pos = text.find(key);
	if (pos == std::string::npos)
		return -1;
	pos = text.find(':', pos);
	if (pos == std::string::npos)
		return -1;

	pos = text.find_first_not_of(" \t\r\n:", pos);
	if (pos == std::string::npos)
		return -1;
	size_t end = pos;
	int64_t value = 0;
	bool any = false;
	for (; end < text.size(); end++) {
		char c = text[end];
		if (c < '0' || c > '9') break;
		value = value * 10 + (c - '0');
		any = true;
	}
	return any ? value : -1;
}

// Reads the total timesteps stored in the most recent numbered checkpoint dir
// (mirrors GGL::Learner::Load's "highest numbered dir" selection).
static int64_t ReadCheckpointTotalTimesteps(const std::string& checkpointFolder) {
	int64_t highest = -1;
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(checkpointFolder, ec)) {
		if (ec) break;
		if (!entry.is_directory())
			continue;
		const std::string name = entry.path().filename().string();
		bool isNumber = !name.empty() && std::all_of(name.begin(), name.end(),
			[](unsigned char c) { return std::isdigit(c) != 0; });
		if (!isNumber)
			continue;
		int64_t ts = 0;
		try { ts = std::stoll(name); } catch (...) { continue; }
		highest = RS_MAX(highest, ts);
	}
	if (highest == -1)
		return 0;

	const std::string statsPath = (std::filesystem::path(checkpointFolder) / std::to_string(highest) / "RUNNING_STATS.json").string();
	int64_t ts = FindTotalTimestepsInJson(statsPath);
	return (ts >= 0) ? ts : highest;
}

RLGC::EnvCreateResult EnvCreateFunc(int index) {
	auto arena = Arena::Create(GameMode::SOCCAR);
	arena->AddCar(Team::BLUE);
	arena->AddCar(Team::ORANGE);

	auto obsBuilder = new R3maJOBS(120.f);
	auto actionParser = new DefaultAction();
	auto stateSetter = g_replayPath.empty() ? new R3maJStateSetter() : new R3maJStateSetter(g_replayPath);

	auto goalScore = new GoalScoreCondition();
	auto noTouch = new NoTouchCondition(10);

	// Curriculum-selected reward weights (from total timesteps via PhaseManager)
	const PhaseRewards& R = g_rewards;

	std::vector<WeightedReward> subRewards;

	auto goalReward = new GoalReward();
	if (R.goal_w != 0.f)
		subRewards.push_back({ goalReward, R.goal_w });
	else
		delete goalReward;

	auto goalDistRaw = new GoalDistanceReward();
	auto goalDistReward = new DeltaReward<GoalDistanceReward>(goalDistRaw);
	if (R.goal_dist_w != 0.f)
		subRewards.push_back({ goalDistReward, R.goal_dist_w });
	else
		delete goalDistReward;

	auto goalSpeedReward = new GoalSpeedBonusReward();
	if (R.goal_speed_bonus_w != 0.f)
		subRewards.push_back({ goalSpeedReward, R.goal_speed_bonus_w });
	else
		delete goalSpeedReward;

	auto goalDistBonusReward = new GoalDistBonusReward();
	if (R.goal_dist_bonus_w != 0.f)
		subRewards.push_back({ goalDistBonusReward, R.goal_dist_bonus_w });
	else
		delete goalDistBonusReward;

	auto touchHeightReward = new TouchHeightReward();
	if (R.touch_height_w != 0.f)
		subRewards.push_back({ touchHeightReward, R.touch_height_w });
	else
		delete touchHeightReward;

	auto touchAccelReward = new NectoTouchAccelReward();
	if (R.touch_accel_w != 0.f)
		subRewards.push_back({ touchAccelReward, R.touch_accel_w });
	else
		delete touchAccelReward;

	if (R.win_prob_w != 0.f) {
		auto winProbReward = new WinProbReward(120, 8);
		subRewards.push_back({ winProbReward, R.win_prob_w });
	}

	if (R.flip_reset_w != 0.f) {
		auto flipResetReward = new FlipResetReward();
		subRewards.push_back({ flipResetReward, R.flip_reset_w });
	}

	auto boostGainReward = new PickupBoostReward();
	if (R.boost_gain_w != 0.f)
		subRewards.push_back({ boostGainReward, R.boost_gain_w });
	else
		delete boostGainReward;

	auto boostLoss = new BoostUsagePenalty();
	if (R.boost_lose_w != 0.f)
		subRewards.push_back({ boostLoss, R.boost_lose_w });
	else
		delete boostLoss;

	auto playerQuality = new PlayerQualityReward(R.dist_w, R.align_w);
	subRewards.push_back({ playerQuality, 1.0f }); // PlayerQuality = 1.0

	// R3maJ: demo weight applied to DemoReward (single split, no demoed penalty)
	auto demoReward = new RLGC::DemoReward();
	if (R.demo_w != 0.f)
		subRewards.push_back({ demoReward, R.demo_w });
	else
		delete demoReward;

	auto angVelReward = new AngVelReward();
	if (R.ang_vel_w != 0.f)
		subRewards.push_back({ angVelReward, R.ang_vel_w });
	else
		delete angVelReward;

	auto groundIdle = new GroundIdlePenalty();
	if (R.touch_grass_w != 0.f)
		subRewards.push_back({ groundIdle, R.touch_grass_w });
	else
		delete groundIdle;

	if (R.jump_touch_w != 0.f) {
		auto jumpTouchReward = new JumpTouchReward();
		subRewards.push_back({ jumpTouchReward, R.jump_touch_w });
	}

	if (R.wall_touch_w != 0.f) {
		auto wallTouchReward = new WallTouchReward();
		subRewards.push_back({ wallTouchReward, R.wall_touch_w });
	}

	if (R.air_dribble_w != 0.f) {
		auto airDribbleReward = new AirDribbleReward();
		subRewards.push_back({ airDribbleReward, R.air_dribble_w });
	}

	if (R.cradle_w != 0.f) {
		auto cradleReward = new CradleReward();
		subRewards.push_back({ cradleReward, R.cradle_w });
	}

	auto allRewards = new AllRewardsWrapper(subRewards, R.opponent_punish_w);

	std::vector<WeightedReward> rewards = {
		{ allRewards, 1.0f }
	};

	std::vector<TerminalCondition*> termConditions = { goalScore, noTouch };

	return RLGC::EnvCreateResult{
		.arena = arena,
		.rewards = rewards,
		.terminalConditions = termConditions,
		.obsBuilder = obsBuilder,
		.actionParser = actionParser,
		.stateSetter = stateSetter
	};
}

void StepCallback(Learner* learner, const std::vector<GameState>& states, Report& report) {
	if (g_quitRequested) {
		RG_LOG("SIGTERM/SIGINT received, saving checkpoint and exiting...");
		if (!learner->config.checkpointFolder.empty())
			learner->Save();
		std::_Exit(0);
	}

	bool expensive = (rand() % 4) == 0;
	for (auto& state : states) {
		if (expensive) {
			const BallState& ball = state.ball;
			report.AddAvg("Ball/Height", ball.pos.z);
			report.AddAvg("Ball/Speed", ball.vel.Length());
			report.AddAvg("Ball/AngularSpeed", ball.angVel.Length());
			report.AddAvg("Game/GoalScored", state.goalScored ? 1 : 0);
			report.AddAvg("Game/LastTouchDelta", state.deltaTime);

			for (auto& player : state.players) {
				float ownGoalY = (player.team == Team::BLUE) ? -5120.f : 5120.f;
				bool onWall = (std::abs(player.pos.x) > 3500.f && player.pos.z > 400.f)
						   || (std::abs(player.pos.y) > 4900.f && player.pos.z > 400.f);

				report.AddAvg("Player/Speed", player.vel.Length());
				report.AddAvg("Player/Height", player.pos.z);
				report.AddAvg("Player/Boost", player.boost);
				report.AddAvg("Player/Airborne", !player.isOnGround);
				report.AddAvg("Player/BallTouch", player.ballTouchedStep);
				report.AddAvg("Player/DistToBall", (player.pos - state.ball.pos).Length());
				report.AddAvg("Player/DistToOwnGoal", (player.pos - Vec(0, ownGoalY, 0)).Length());
				report.AddAvg("Player/Supersonic", player.isSupersonic);
				report.AddAvg("Player/Flipping", (player.isFlipping || player.hasFlipped) ? 1 : 0);
				report.AddAvg("Player/HasDoubleJumped", player.hasDoubleJumped);
				report.AddAvg("Player/Jumping", (player.hasJumped || player.isJumping) ? 1 : 0);
				report.AddAvg("Player/AirTime", player.airTime);
				report.AddAvg("Player/TimeSpentBoosting", player.timeSpentBoosting);
				report.AddAvg("Player/OnWall", onWall ? 1 : 0);
				report.AddAvg("Events/Goal", player.eventState.goal ? 1 : 0);
				report.AddAvg("Events/Save", player.eventState.save ? 1 : 0);
				report.AddAvg("Events/Shot", player.eventState.shot ? 1 : 0);
				report.AddAvg("Events/Bump", player.eventState.bump ? 1 : 0);
				report.AddAvg("Events/Demo", player.eventState.demo ? 1 : 0);
				report.AddAvg("Events/Demoed", player.eventState.demoed ? 1 : 0);
			}
		}
	}
}

static void PrintUsage(const char* progName) {
	RG_LOG("Usage: " << progName << " [options]");
	RG_LOG("  --resume <dir>    Load checkpoint from directory");
	RG_LOG("  --device <type>   Device type: cpu, cuda (default: cpu)");
	RG_LOG("  --games <n>       Number of parallel games (default: 32)");
	RG_LOG("  --save-dir <dir>  Checkpoint save/load directory (default: checkpoints)");
	RG_LOG("  --phase <n>       Force phase index 0-3 (default: auto from checkpoint timesteps)");
	RG_LOG("  --replays <path>  Binary replay file for state initialization");
}

int main(int argc, char* argv[]) {
	SetupSignalHandlers();

	// Parse CLI args
	int phaseIdx = -1; // -1 = auto-select from totalTimesteps
	std::string resumeDir;
	std::string deviceStr = "cpu";
	std::string saveDir = "checkpoints";
	std::string replayPath;
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
			phaseIdx = std::max(0, std::min(phaseIdx, 3));
		} else if (strcmp(argv[i], "--replays") == 0 && i + 1 < argc) {
			replayPath = argv[++i];
		} else {
			RG_LOG("Unknown option: " << argv[i]);
			PrintUsage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	RocketSim::Init("collision_meshes");

	// Resolve phase from total timesteps (resume checkpoint wins, else fresh run at 0).
	std::string checkpointFolder = !resumeDir.empty() ? resumeDir : saveDir;
	g_totalTimesteps = ReadCheckpointTotalTimesteps(checkpointFolder);

	if (phaseIdx == -1)
		phaseIdx = g_PhaseManager.GetCurrentPhase(g_totalTimesteps);

	g_rewards = g_PhaseManager.GetRewards(g_totalTimesteps);

	LearnerConfig cfg = g_PhaseManager.MakeLearnerConfig(phaseIdx);

	cfg.checkpointFolder = checkpointFolder;

	if (deviceStr == "cuda")
		cfg.deviceType = GGL::LearnerDeviceType::GPU_CUDA;
	else
		cfg.deviceType = GGL::LearnerDeviceType::CPU;

	if (numGames > 0)
		cfg.numGames = numGames;

	if (!replayPath.empty())
		g_replayPath = replayPath;

	// wandb logging only when WANDB_API_KEY is present (avoids hangs without a key)
	cfg.sendMetrics = std::getenv("WANDB_API_KEY") != nullptr;

	RG_LOG(RG_DIVIDER);
	RG_LOG("=== R3maJ v1 ===");
	RG_LOG("  Timesteps: " << g_totalTimesteps);
	RG_LOG("  Phase: " << (phaseIdx + 1) << "/4 (auto-selected from timesteps)");
	const auto& phaseCfg = g_PhaseManager.GetPhaseConfig(phaseIdx);
	RG_LOG("  Steps: " << phaseCfg.startStep << " - " << phaseCfg.endStep);
	RG_LOG("  Device: " << (deviceStr == "cuda" ? "CUDA" : "CPU"));
	RG_LOG("  Games: " << cfg.numGames);
	RG_LOG("  Tick Skip: " << cfg.tickSkip);
	RG_LOG("  Action Delay: " << cfg.actionDelay);
	RG_LOG("  Network: shared 512x2 -> policy/critic 512x6");
	RG_LOG("  Epochs: " << cfg.ppo.epochs);
	RG_LOG("  LR: " << cfg.ppo.policyLR);
	RG_LOG("  Gamma: " << phaseCfg.gamma);
	RG_LOG("  Entropy: " << phaseCfg.entropyScale);
	RG_LOG("  MiniBatch: " << cfg.ppo.miniBatchSize);
	RG_LOG("  Checkpoints: " << cfg.checkpointFolder);
	RG_LOG("  State Init: R3maJMultiModal (Kickoff/Ground/Goalie/Aerial/Wall/Dribble)");
	RG_LOG("  Rewards: goal=" << g_rewards.goal_w
		<< " winprob=" << g_rewards.win_prob_w
		<< " goaldist=" << g_rewards.goal_dist_w
		<< " speedbonus=" << g_rewards.goal_speed_bonus_w
		<< " touchheight=" << g_rewards.touch_height_w
		<< " touchaccel=" << g_rewards.touch_accel_w
		<< " boostgain=" << g_rewards.boost_gain_w
		<< " boostlose=" << g_rewards.boost_lose_w
		<< " demo=" << g_rewards.demo_w
		<< " flipreset=" << g_rewards.flip_reset_w
		<< " jungtouch=" << g_rewards.jump_touch_w
		<< " walltouch=" << g_rewards.wall_touch_w
		<< " airdribble=" << g_rewards.air_dribble_w
		<< " cradle=" << g_rewards.cradle_w
		<< " angvel=" << g_rewards.ang_vel_w
		<< " opppunish=" << g_rewards.opponent_punish_w);

	Learner* learner = new Learner(EnvCreateFunc, cfg, StepCallback);
	g_learner = learner;

	RG_LOG("  Actions: " << learner->numActions);
	RG_LOG("  Obs Size: " << learner->obsSize);
	RG_LOG(RG_DIVIDER);

	learner->Start();

	delete learner;
	g_learner = nullptr;
	return EXIT_SUCCESS;
}