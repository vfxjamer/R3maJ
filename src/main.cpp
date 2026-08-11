#include <csignal>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <algorithm>
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

void SignalHandler(int) {
	g_quitRequested = true;
}

void SetupSignalHandlers() {
	std::signal(SIGINT, SignalHandler);
	std::signal(SIGTERM, SignalHandler);
}

RLGC::EnvCreateResult EnvCreateFunc(int index) {
	auto arena = Arena::Create(GameMode::SOCCAR);
	arena->AddCar(Team::BLUE);
	arena->AddCar(Team::ORANGE);

	auto obsBuilder = new R3maJOBS(120.f);
	auto actionParser = new DefaultAction();
	auto stateSetter = g_replayPath.empty() ? new R3maJStateSetter() : new R3maJStateSetter(g_replayPath);

	auto goalScore = new GoalScoreCondition();
	auto noTouch = new NoTouchCondition(68);

	// Necto-derived reward weights (identical across all phases)
	constexpr float
		GOAL_W = 10.f,
		WIN_PROB_W = 10.f,
		GOAL_DIST_W = 10.f,
		GOAL_SPEED_BONUS_W = 2.5f,
		GOAL_DIST_BONUS_W = 2.5f,
		TOUCH_HEIGHT_W = 3.f,
		TOUCH_ACCEL_W = 0.5f,
		FLIP_RESET_W = 10.f,
		DEMO_W = 8.f,
		BOOST_GAIN_W = 1.5f,
		BOOST_LOSE_W = 0.8f,
		DIST_W = 0.25f,
		ALIGN_W = 0.25f,
		ANG_VEL_W = 0.005f,
		TOUCH_GRASS_W = 0.005f,
		OPPONENT_PUNISH_W = 1.f,
		JUMP_TOUCH_W = 6.f,
		WALL_TOUCH_W = 2.f,
		AIR_DRIBBLE_W = 2.f,
		CRADLE_W = 1.f;

	std::vector<WeightedReward> subRewards;

	auto goalReward = new GoalReward();
	subRewards.push_back({ goalReward, GOAL_W });

	auto goalDistRaw = new GoalDistanceReward();
	auto goalDistReward = new DeltaReward<GoalDistanceReward>(goalDistRaw);
	subRewards.push_back({ goalDistReward, GOAL_DIST_W });

	auto goalSpeedReward = new GoalSpeedBonusReward();
	subRewards.push_back({ goalSpeedReward, GOAL_SPEED_BONUS_W });

	auto goalDistBonusReward = new GoalDistBonusReward();
	subRewards.push_back({ goalDistBonusReward, GOAL_DIST_BONUS_W });

	auto touchHeightReward = new TouchHeightReward();
	subRewards.push_back({ touchHeightReward, TOUCH_HEIGHT_W });

	auto touchAccelReward = new NectoTouchAccelReward();
	subRewards.push_back({ touchAccelReward, TOUCH_ACCEL_W });

	if (WIN_PROB_W != 0.f) {
		auto winProbReward = new WinProbReward(120, 8);
		subRewards.push_back({ winProbReward, WIN_PROB_W });
	}

	if (FLIP_RESET_W != 0.f) {
		auto flipResetReward = new FlipResetReward();
		subRewards.push_back({ flipResetReward, FLIP_RESET_W });
	}

	auto boostGainReward = new PickupBoostReward();
	subRewards.push_back({ boostGainReward, BOOST_GAIN_W });

	auto boostLoss = new BoostUsagePenalty();
	subRewards.push_back({ boostLoss, BOOST_LOSE_W });

	auto playerQuality = new PlayerQualityReward(DIST_W, ALIGN_W);
	subRewards.push_back({ playerQuality, 1.0f });

	auto demoReward = new RLGC::DemoReward();
	subRewards.push_back({ demoReward, DEMO_W * 0.5f });

	auto demoedPenalty = new RLGC::DemoedPenalty();
	subRewards.push_back({ demoedPenalty, DEMO_W * 0.5f });

	auto angVelReward = new AngVelReward();
	subRewards.push_back({ angVelReward, ANG_VEL_W });

	auto groundIdle = new GroundIdlePenalty();
	subRewards.push_back({ groundIdle, TOUCH_GRASS_W });

	if (JUMP_TOUCH_W != 0.f) {
		auto jumpTouchReward = new JumpTouchReward();
		subRewards.push_back({ jumpTouchReward, JUMP_TOUCH_W });
	}

	if (WALL_TOUCH_W != 0.f) {
		auto wallTouchReward = new WallTouchReward();
		subRewards.push_back({ wallTouchReward, WALL_TOUCH_W });
	}

	if (AIR_DRIBBLE_W != 0.f) {
		auto airDribbleReward = new AirDribbleReward();
		subRewards.push_back({ airDribbleReward, AIR_DRIBBLE_W });
	}

	if (CRADLE_W != 0.f) {
		auto cradleReward = new CradleReward();
		subRewards.push_back({ cradleReward, CRADLE_W });
	}

	auto allRewards = new AllRewardsWrapper(subRewards, OPPONENT_PUNISH_W);

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
	RG_LOG("  --phase <n>       Start at phase index 0-3 (default: 0)");
	RG_LOG("  --replays <path>  Binary replay file for state initialization");
}

int main(int argc, char* argv[]) {
	SetupSignalHandlers();

	// Parse CLI args
	int phaseIdx = 0;
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

	LearnerConfig cfg = g_PhaseManager.MakeLearnerConfig(phaseIdx);

	if (!resumeDir.empty())
		cfg.checkpointFolder = resumeDir;
	else if (saveDir != "checkpoints")
		cfg.checkpointFolder = saveDir;

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
	RG_LOG("  Phase: " << (phaseIdx + 1) << "/4");
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
	RG_LOG("  Press Q to quit");

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
