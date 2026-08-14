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
static int g_phaseIdx = 0;
static std::string g_deviceStr = "cpu";
static int g_requestedGames = -1;
static int64_t g_metricCalls = 0;

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

// Reads the total timesteps stored in the most recent numbered checkpoint dir.
static int64_t ReadCheckpointTotalTimesteps(const std::string& checkpointFolder) {
	int64_t highest = -1;
	std::error_code ec;
	if (!std::filesystem::exists(checkpointFolder, ec))
		return 0;

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
	const PhaseRewards& R = g_rewards;
	std::vector<WeightedReward> subRewards;

	auto goalReward = new GoalReward();
	if (R.goal_w != 0.f) subRewards.push_back({ goalReward, R.goal_w }); else delete goalReward;
	auto goalDistRaw = new GoalDistanceReward();
	auto goalDistReward = new DeltaReward<GoalDistanceReward>(goalDistRaw);
	if (R.goal_dist_w != 0.f) subRewards.push_back({ goalDistReward, R.goal_dist_w }); else delete goalDistReward;
	auto goalSpeedReward = new GoalSpeedBonusReward();
	if (R.goal_speed_bonus_w != 0.f) subRewards.push_back({ goalSpeedReward, R.goal_speed_bonus_w }); else delete goalSpeedReward;
	auto goalDistBonusReward = new GoalDistBonusReward();
	if (R.goal_dist_bonus_w != 0.f) subRewards.push_back({ goalDistBonusReward, R.goal_dist_bonus_w }); else delete goalDistBonusReward;
	auto touchHeightReward = new TouchHeightReward();
	if (R.touch_height_w != 0.f) subRewards.push_back({ touchHeightReward, R.touch_height_w }); else delete touchHeightReward;
	auto touchAccelReward = new NectoTouchAccelReward();
	if (R.touch_accel_w != 0.f) subRewards.push_back({ touchAccelReward, R.touch_accel_w }); else delete touchAccelReward;
	if (R.win_prob_w != 0.f) {
		auto winProbReward = new WinProbReward(120, 8);
		subRewards.push_back({ winProbReward, R.win_prob_w });
	}
	if (R.flip_reset_w != 0.f) {
		auto flipResetReward = new FlipResetReward();
		subRewards.push_back({ flipResetReward, R.flip_reset_w });
	}
	auto boostGainReward = new PickupBoostReward();
	if (R.boost_gain_w != 0.f) subRewards.push_back({ boostGainReward, R.boost_gain_w }); else delete boostGainReward;
	auto boostLoss = new BoostUsagePenalty();
	if (R.boost_lose_w != 0.f) subRewards.push_back({ boostLoss, R.boost_lose_w }); else delete boostLoss;
	auto playerQuality = new PlayerQualityReward(R.dist_w, R.align_w);
	subRewards.push_back({ playerQuality, 1.0f });
	auto demoReward = new RLGC::DemoReward();
	if (R.demo_w != 0.f) subRewards.push_back({ demoReward, R.demo_w }); else delete demoReward;
	auto angVelReward = new AngVelReward();
	if (R.ang_vel_w != 0.f) subRewards.push_back({ angVelReward, R.ang_vel_w }); else delete angVelReward;
	auto groundIdle = new GroundIdlePenalty();
	if (R.touch_grass_w != 0.f) subRewards.push_back({ groundIdle, R.touch_grass_w }); else delete groundIdle;
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
	std::vector<WeightedReward> rewards = {{ allRewards, 1.0f }};
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

// All native telemetry is emitted through GigaLearn's Report, so the same
// metrics flow to W&B when cfg.sendMetrics is enabled. This keeps experiment
// instrumentation in the C++ core instead of depending on notebook code.
void StepCallback(Learner* learner, const std::vector<GameState>& states, Report& report) {
	if (g_quitRequested) {
		RG_LOG("SIGTERM/SIGINT received, saving checkpoint and exiting...");
		if (!learner->config.checkpointFolder.empty())
			learner->Save();
		std::_Exit(0);
	}

	++g_metricCalls;

	// Run/config telemetry. These are intentionally stable metrics so a W&B run
	// contains enough information to reconstruct the exact training setup.
	report.AddAvg("Run/Phase", static_cast<float>(g_phaseIdx + 1));
	report.AddAvg("Run/TotalTimestepsAtStart", static_cast<float>(g_totalTimesteps));
	report.AddAvg("Run/Games", static_cast<float>(g_requestedGames > 0 ? g_requestedGames : 0));
	report.AddAvg("Run/MetricCallbackCount", static_cast<float>(g_metricCalls));
	report.AddAvg("Config/GoalRewardWeight", g_rewards.goal_w);
	report.AddAvg("Config/GoalDistanceWeight", g_rewards.goal_dist_w);
	report.AddAvg("Config/GoalSpeedBonusWeight", g_rewards.goal_speed_bonus_w);
	report.AddAvg("Config/GoalDistanceBonusWeight", g_rewards.goal_dist_bonus_w);
	report.AddAvg("Config/TouchHeightWeight", g_rewards.touch_height_w);
	report.AddAvg("Config/TouchAccelerationWeight", g_rewards.touch_accel_w);
	report.AddAvg("Config/WinProbabilityWeight", g_rewards.win_prob_w);
	report.AddAvg("Config/FlipResetWeight", g_rewards.flip_reset_w);
	report.AddAvg("Config/BoostGainWeight", g_rewards.boost_gain_w);
	report.AddAvg("Config/BoostLossWeight", g_rewards.boost_lose_w);
	report.AddAvg("Config/DemoWeight", g_rewards.demo_w);
	report.AddAvg("Config/AngularVelocityWeight", g_rewards.ang_vel_w);
	report.AddAvg("Config/GroundIdleWeight", g_rewards.touch_grass_w);
	report.AddAvg("Config/JumpTouchWeight", g_rewards.jump_touch_w);
	report.AddAvg("Config/WallTouchWeight", g_rewards.wall_touch_w);
	report.AddAvg("Config/AirDribbleWeight", g_rewards.air_dribble_w);
	report.AddAvg("Config/CradleWeight", g_rewards.cradle_w);
	report.AddAvg("Config/OpponentPunishWeight", g_rewards.opponent_punish_w);
	report.AddAvg("Config/PlayerDistanceWeight", g_rewards.dist_w);
	report.AddAvg("Config/PlayerAlignmentWeight", g_rewards.align_w);

	// Learner-level information available from the native callback.
	report.AddAvg("Learner/NumActions", static_cast<float>(learner->numActions));
	report.AddAvg("Learner/ObsSize", static_cast<float>(learner->obsSize));

	// Gameplay telemetry. Expensive per-player state inspection is sampled at
	// 25%, while the learner/config metrics above are emitted every callback.
	const bool expensive = (g_metricCalls % 4) == 0;
	if (!expensive)
		return;

	for (const auto& state : states) {
		const BallState& ball = state.ball;
		report.AddAvg("Ball/Height", ball.pos.z);
		report.AddAvg("Ball/Speed", ball.vel.Length());
		report.AddAvg("Ball/AngularSpeed", ball.angVel.Length());
		report.AddAvg("Ball/PositionX", ball.pos.x);
		report.AddAvg("Ball/PositionY", ball.pos.y);
		report.AddAvg("Ball/VelocityX", ball.vel.x);
		report.AddAvg("Ball/VelocityY", ball.vel.y);
		report.AddAvg("Ball/VelocityZ", ball.vel.z);
		report.AddAvg("Ball/GoalScored", state.goalScored ? 1.f : 0.f);
		report.AddAvg("Game/LastTouchDelta", state.deltaTime);

		for (const auto& player : state.players) {
			const float ownGoalY = (player.team == Team::BLUE) ? -5120.f : 5120.f;
			const float ballDistance = (player.pos - state.ball.pos).Length();
			const bool onWall = (std::abs(player.pos.x) > 3500.f && player.pos.z > 400.f)
				|| (std::abs(player.pos.y) > 4900.f && player.pos.z > 400.f);

			report.AddAvg("Player/Speed", player.vel.Length());
			report.AddAvg("Player/Height", player.pos.z);
			report.AddAvg("Player/Boost", player.boost);
			report.AddAvg("Player/Airborne", player.isOnGround ? 0.f : 1.f);
			report.AddAvg("Player/BallTouch", player.ballTouchedStep ? 1.f : 0.f);
			report.AddAvg("Player/DistToBall", ballDistance);
			report.AddAvg("Player/DistToOwnGoal", (player.pos - Vec(0, ownGoalY, 0)).Length());
			report.AddAvg("Player/Supersonic", player.isSupersonic ? 1.f : 0.f);
			report.AddAvg("Player/Flipping", (player.isFlipping || player.hasFlipped) ? 1.f : 0.f);
			report.AddAvg("Player/HasDoubleJumped", player.hasDoubleJumped ? 1.f : 0.f);
			report.AddAvg("Player/Jumping", (player.hasJumped || player.isJumping) ? 1.f : 0.f);
			report.AddAvg("Player/AirTime", player.airTime);
			report.AddAvg("Player/TimeSpentBoosting", player.timeSpentBoosting);
			report.AddAvg("Player/OnWall", onWall ? 1.f : 0.f);
			report.AddAvg("Player/VelocityX", player.vel.x);
			report.AddAvg("Player/VelocityY", player.vel.y);
			report.AddAvg("Player/VelocityZ", player.vel.z);
			report.AddAvg("Events/Goal", player.eventState.goal ? 1.f : 0.f);
			report.AddAvg("Events/Save", player.eventState.save ? 1.f : 0.f);
			report.AddAvg("Events/Shot", player.eventState.shot ? 1.f : 0.f);
			report.AddAvg("Events/Bump", player.eventState.bump ? 1.f : 0.f);
			report.AddAvg("Events/Demo", player.eventState.demo ? 1.f : 0.f);
			report.AddAvg("Events/Demoed", player.eventState.demoed ? 1.f : 0.f);
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

	int phaseIdx = -1;
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

	std::string checkpointFolder = !resumeDir.empty() ? resumeDir : saveDir;
	g_totalTimesteps = ReadCheckpointTotalTimesteps(checkpointFolder);

	if (phaseIdx == -1)
		phaseIdx = g_PhaseManager.GetCurrentPhase(g_totalTimesteps);
	g_phaseIdx = phaseIdx;
	g_rewards = g_PhaseManager.GetRewards(g_totalTimesteps);

	LearnerConfig cfg = g_PhaseManager.MakeLearnerConfig(phaseIdx);
	cfg.checkpointFolder = checkpointFolder;

	if (deviceStr == "cuda")
		cfg.deviceType = GGL::LearnerDeviceType::GPU_CUDA;
	else
		cfg.deviceType = GGL::LearnerDeviceType::CPU;

	if (numGames > 0)
		cfg.numGames = numGames;
	g_requestedGames = cfg.numGames;

	if (!replayPath.empty())
		g_replayPath = replayPath;

	// Native telemetry is enabled by the same environment credential used by
	// the Colab launcher. The C++ core does not need to know or print the secret.
	cfg.sendMetrics = std::getenv("WANDB_API_KEY") != nullptr;

	RG_LOG(RG_DIVIDER);
	RG_LOG("=== R3maJ v2 — Native W&B Telemetry ===");
	RG_LOG("  Timesteps at start: " << g_totalTimesteps);
	RG_LOG("  Phase: " << (phaseIdx + 1) << "/4");
	const auto& phaseCfg = g_PhaseManager.GetPhaseConfig(phaseIdx);
	RG_LOG("  Phase range: " << phaseCfg.startStep << " - " << phaseCfg.endStep);
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
	RG_LOG("  W&B native metrics: " << (cfg.sendMetrics ? "ENABLED" : "DISABLED (WANDB_API_KEY missing)"));
	RG_LOG("  Checkpoints: " << cfg.checkpointFolder);
	RG_LOG("  Replay source: " << (g_replayPath.empty() ? "default state setter" : g_replayPath));
	RG_LOG("  Rewards: goal=" << g_rewards.goal_w
		<< " winprob=" << g_rewards.win_prob_w
		<< " goaldist=" << g_rewards.goal_dist_w
		<< " speedbonus=" << g_rewards.goal_speed_bonus_w
		<< " goaldistbonus=" << g_rewards.goal_dist_bonus_w
		<< " touchheight=" << g_rewards.touch_height_w
		<< " touchaccel=" << g_rewards.touch_accel_w
		<< " boostgain=" << g_rewards.boost_gain_w
		<< " boostlose=" << g_rewards.boost_lose_w
		<< " demo=" << g_rewards.demo_w
		<< " flipreset=" << g_rewards.flip_reset_w
		<< " jumptouch=" << g_rewards.jump_touch_w
		<< " walltouch=" << g_rewards.wall_touch_w
		<< " airdribble=" << g_rewards.air_dribble_w
		<< " cradle=" << g_rewards.cradle_w
		<< " angvel=" << g_rewards.ang_vel_w
		<< " groundidle=" << g_rewards.touch_grass_w
		<< " opppunish=" << g_rewards.opponent_punish_w);

	Learner* learner = new Learner(EnvCreateFunc, cfg, StepCallback);
	g_learner = learner;

	RG_LOG("  Actions: " << learner->numActions);
	RG_LOG("  Obs Size: " << learner->obsSize);
	RG_LOG("  Native W&B telemetry categories: Run, Config, Learner, Ball, Player, Events");
	RG_LOG(RG_DIVIDER);

	learner->Start();

	delete learner;
	g_learner = nullptr;
	return EXIT_SUCCESS;
}
