#pragma once

#include <RLGymCPP/StateSetters/StateSetter.h>
#include <RLGymCPP/Framework.h>

#include <vector>
#include <string>
#include <memory>
#include <mutex>

using namespace RLGC;

struct ReplayFrame {
	Vec ballPos;
	Vec ballVel;
	Vec ballAngVel;

	Vec carPos[2];
	Vec carRotEuler[2];
	Vec carVel[2];
	Vec carAngVel[2];

	float carBoost[2];
	bool carOnGround[2];

	int blueScore;
	int orangeScore;
};

class R3maJStateSetter : public StateSetter {
public:
	enum class Mode {
		KICKOFF,
		GROUND_PLAY,
		GOALIE_PRACTICE,
		AERIAL_PRACTICE,
		WALL_PLAY,
		DRIBBLE_PRACTICE
	};

	R3maJStateSetter();
	R3maJStateSetter(const std::string& replayPath);

	void ResetArena(Arena* arena) override;

private:
	// ---- Shared replay storage ----
	//
	// All 164 environments share these instead
	// of loading a separate copy of the replay
	// dataset for every StateSetter instance.
	static std::shared_ptr<std::vector<ReplayFrame>> _sharedReplayFrames;
	static std::shared_ptr<std::vector<float>> _sharedReplayCumWeights;

	// Guarantees the replay file is loaded only once,
	// even when many environments are created.
	static std::once_flag _replayLoadOnce;

	// ---- Replay functions ----
	bool _LoadReplays(const std::string& path);
	int _SampleReplayFrame() const;

	void _SetFromReplay(
		Arena* arena,
		const ReplayFrame& frame
	) const;

	// ---- Procedural state setters ----
	Mode _pickMode() const;

	void SetGroundPlay(Arena* arena);
	void SetGoaliePractice(Arena* arena);
	void SetAerialPractice(Arena* arena);
	void SetWallPlay(Arena* arena);
	void SetDribblePractice(Arena* arena);
};
