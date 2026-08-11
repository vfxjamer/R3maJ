#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <RLGymCPP/StateSetters/StateSetter.h>

using namespace RLGC;

struct ReplayFrame {
	Vec ballPos, ballVel, ballAngVel;
	Vec carPos[2];
	Vec carRotEuler[2]; // yaw, pitch, roll in radians
	Vec carVel[2], carAngVel[2];
	float carBoost[2];
	bool carOnGround[2];
	int blueScore, orangeScore;
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
	explicit R3maJStateSetter(const std::string& replayPath);

	virtual void ResetArena(Arena* arena) override;

private:
	std::vector<ReplayFrame> _replayFrames;
	std::vector<float> _replayCumWeights;

	bool _LoadReplays(const std::string& path);
	int _SampleReplayFrame() const;

	void _SetFromReplay(Arena* arena, const ReplayFrame& frame) const;

	void SetKickoff(Arena* arena);
	void SetGroundPlay(Arena* arena);
	void SetGoaliePractice(Arena* arena);
	void SetAerialPractice(Arena* arena);
	void SetWallPlay(Arena* arena);
	void SetDribblePractice(Arena* arena);

	Mode _pickMode() const;

	Car* _getCar(Arena* arena, Team team) {
		for (Car* car : arena->_cars)
			if (car->team == team) return car;
		return nullptr;
	}
};
