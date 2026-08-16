#include "R3maJStateSetter.h"
#include "PhaseManager.h"
#include <RLGymCPP/Math.h>
#include <RLGymCPP/CommonValues.h>
#include <fstream>
#include <cmath>

using RocketSim::Math::RandFloat;
using RLGC::Math::RandVec;
using namespace RLGC;

// ---- Shared Replay Storage ----

std::shared_ptr<std::vector<ReplayFrame>>
R3maJStateSetter::_sharedReplayFrames;

std::shared_ptr<std::vector<float>>
R3maJStateSetter::_sharedReplayCumWeights;

std::once_flag R3maJStateSetter::_replayLoadOnce;


// ---- Replay Loading ----

bool R3maJStateSetter::_LoadReplays(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		RG_LOG("R3maJStateSetter: Could not open replay file: " << path);
		return false;
	}

	int64_t nFrames;
	file.read(reinterpret_cast<char*>(&nFrames), sizeof(nFrames));
	if (!file.good() || nFrames <= 0) {
		RG_LOG("R3maJStateSetter: Invalid replay file (nFrames=" << nFrames << ")");
		return false;
	}

	_sharedReplayFrames->resize(nFrames);
	_sharedReplayCumWeights->resize(nFrames);

	double totalWeight = 0.0;

	for (int64_t i = 0; i < nFrames; i++) {
		ReplayFrame& f = (*_sharedReplayFrames)[i];

		auto readVec = [&](Vec& v) {
			file.read(reinterpret_cast<char*>(&v), sizeof(float) * 3);
		};

		auto readFloat = [&](float& v) {
			file.read(reinterpret_cast<char*>(&v), sizeof(float));
		};

		auto readInt = [&](int& v) {
			file.read(reinterpret_cast<char*>(&v), sizeof(int));
		};

		auto readByte = [&](bool& v) {
			uint8_t b;
			file.read(reinterpret_cast<char*>(&b), 1);
			v = (b != 0);
		};

		readVec(f.ballPos);
		readVec(f.ballVel);
		readVec(f.ballAngVel);

		for (int c = 0; c < 2; c++) {
			readVec(f.carPos[c]);
			readVec(f.carRotEuler[c]);
			readVec(f.carVel[c]);
			readVec(f.carAngVel[c]);
			readFloat(f.carBoost[c]);
			readByte(f.carOnGround[c]);
		}

		readInt(f.blueScore);
		readInt(f.orangeScore);

		if (!file.good()) {
			RG_LOG(
				"R3maJStateSetter: Error reading frame "
				<< i << "/" << nFrames
			);

			_sharedReplayFrames->clear();
			_sharedReplayCumWeights->clear();
			return false;
		}

		// Height-weighted sampling:
		// weight = 1 + ball_z / 2000, capped at 10
		float h = f.ballPos.z;
		float weight = 1.f + RS_MIN(h / 2000.f, 9.f);

		totalWeight += weight;
		(*_sharedReplayCumWeights)[i] = (float)totalWeight;
	}

	// Normalize cumulative weights to [0, 1]
	if (totalWeight > 0) {
		for (auto& w : *_sharedReplayCumWeights)
			w /= (float)totalWeight;
	}

	RG_LOG(
		"R3maJStateSetter: Loaded "
		<< _sharedReplayFrames->size()
		<< " replay frames from "
		<< path
	);

	return true;
}


int R3maJStateSetter::_SampleReplayFrame() const {
	if (!_sharedReplayFrames || _sharedReplayFrames->empty())
		return -1;

	float r = RandFloat(0, 1);

	for (int i = 0; i < (int)_sharedReplayCumWeights->size(); i++) {
		if (r <= (*_sharedReplayCumWeights)[i])
			return i;
	}

	return (int)_sharedReplayCumWeights->size() - 1;
}


void R3maJStateSetter::_SetFromReplay(
	Arena* arena,
	const ReplayFrame& frame
) const {
	BallState bs = {};
	bs.pos = frame.ballPos;
	bs.vel = frame.ballVel;
	bs.angVel = frame.ballAngVel;
	arena->ball->SetState(bs);

	for (Car* car : arena->_cars) {
		int idx = (car->team == Team::ORANGE) ? 1 : 0;

		CarState cs = {};
		cs.pos = frame.carPos[idx];
		cs.vel = frame.carVel[idx];
		cs.angVel = frame.carAngVel[idx];
		cs.boost = frame.carBoost[idx];
		cs.isOnGround = frame.carOnGround[idx];

		cs.rotMat = Angle(
			frame.carRotEuler[idx].x,
			frame.carRotEuler[idx].y,
			frame.carRotEuler[idx].z
		).ToRotMat();

		car->SetState(cs);
	}
}


// ---- Procedural Modes ----

R3maJStateSetter::R3maJStateSetter() {}


R3maJStateSetter::R3maJStateSetter(
	const std::string& replayPath
) {
	// Load the replay dataset exactly once.
	//
	// All R3maJStateSetter instances share the same
	// replay vectors instead of loading 1.8M frames
	// independently.
	std::call_once(_replayLoadOnce, [&]() {
		_sharedReplayFrames =
			std::make_shared<std::vector<ReplayFrame>>();

		_sharedReplayCumWeights =
			std::make_shared<std::vector<float>>();

		if (!_LoadReplays(replayPath)) {
			_sharedReplayFrames->clear();
			_sharedReplayCumWeights->clear();

			RG_LOG(
				"R3maJStateSetter: Failed to load shared replay pool."
			);
		}
	});
}


R3maJStateSetter::Mode R3maJStateSetter::_pickMode() const {
	float r = RandFloat(0, 1);

	if (r < 0.20f)
		return Mode::KICKOFF;

	if (r < 0.45f)
		return Mode::GROUND_PLAY;

	if (r < 0.60f)
		return Mode::GOALIE_PRACTICE;

	if (r < 0.75f)
		return Mode::AERIAL_PRACTICE;

	if (r < 0.85f)
		return Mode::WALL_PLAY;

	return Mode::DRIBBLE_PRACTICE;
}


void R3maJStateSetter::ResetArena(Arena* arena) {
	// Until REPLAY_STATE_START (15B steps): every episode starts like a real
	// Rocket League match — ball at centre, cars at kickoff spots.
	if (g_trainingTimesteps < REPLAY_STATE_START) {
		arena->ResetToRandomKickoff();
		return;
	}

	// Beyond 15B steps: start from real-match replay frames (recovery /
	// mid-game scenarios) or procedural play states half the time, and a
	// fresh kickoff the rest.
	if (_sharedReplayFrames && !_sharedReplayFrames->empty() && RandFloat(0, 1) < 0.5f) {
		int idx = _SampleReplayFrame();
		_SetFromReplay(arena, (*_sharedReplayFrames)[idx]);
		return;
	}

	switch (_pickMode()) {
		case Mode::KICKOFF:           arena->ResetToRandomKickoff(); break;
		case Mode::GROUND_PLAY:       SetGroundPlay(arena); break;
		case Mode::GOALIE_PRACTICE:   SetGoaliePractice(arena); break;
		case Mode::AERIAL_PRACTICE:   SetAerialPractice(arena); break;
		case Mode::WALL_PLAY:         SetWallPlay(arena); break;
		case Mode::DRIBBLE_PRACTICE:  SetDribblePractice(arena); break;
	}
}


void R3maJStateSetter::SetGroundPlay(Arena* arena) {
	BallState bs = {};

	bs.pos = RandVec(
		Vec(-1500, -1500, CommonValues::BALL_RADIUS),
		Vec(1500, 1500, CommonValues::BALL_RADIUS + 50)
	);

	bs.vel = RandVec(
		Vec(-1000, -1000, 0),
		Vec(1000, 1000, 0)
	);

	arena->ball->SetState(bs);

	for (Car* car : arena->_cars) {
		CarState cs = {};

		Vec offset =
			(car->team == Team::BLUE)
			? Vec(0, -800, 0)
			: Vec(0, 800, 0);

		cs.pos =
			RandVec(
				Vec(-1200, -1200, 17),
				Vec(1200, 1200, 17)
			) + offset;

		cs.vel = RandVec(
			Vec(-500, -500, 0),
			Vec(500, 500, 0)
		);

		Angle facingBall =
			Angle::FromVec(
				(bs.pos - cs.pos).Normalized()
			);

		cs.rotMat = facingBall.ToRotMat();
		cs.boost = RandFloat(30, 100);
		cs.isOnGround = true;

		car->SetState(cs);
	}
}


void R3maJStateSetter::SetGoaliePractice(Arena* arena) {
	bool ballTowardBlue = RandFloat() > 0.5f;
	float yDir = ballTowardBlue ? -1.f : 1.f;

	BallState bs = {};

	bs.pos = Vec(
		RandFloat(-1000, 1000),
		yDir * RandFloat(2000, 4000),
		CommonValues::BALL_RADIUS
	);

	bs.vel = Vec(
		RandFloat(-500, 500),
		yDir * RandFloat(1000, 2500),
		RandFloat(0, 400)
	);

	arena->ball->SetState(bs);

	for (Car* car : arena->_cars) {
		CarState cs = {};

		if (car->team == Team::BLUE) {
			if (ballTowardBlue) {
				cs.pos = Vec(
					RandFloat(-500, 500),
					RandFloat(-5200, -4500),
					17
				);
			}
			else {
				cs.pos = Vec(
					RandFloat(-500, 500),
					RandFloat(-2000, -1000),
					17
				);
			}
		}
		else {
			if (!ballTowardBlue) {
				cs.pos = Vec(
					RandFloat(-500, 500),
					RandFloat(4500, 5200),
					17
				);
			}
			else {
				cs.pos = Vec(
					RandFloat(-500, 500),
					RandFloat(1000, 2000),
					17
				);
			}
		}

		Vec toBall =
			(bs.pos - cs.pos).Normalized();

		cs.rotMat = Angle::FromVec(toBall).ToRotMat();
		cs.boost = RandFloat(30, 100);
		cs.isOnGround = true;

		car->SetState(cs);
	}
}


void R3maJStateSetter::SetAerialPractice(Arena* arena) {
	BallState bs = {};

	bs.pos = Vec(
		RandFloat(-500, 500),
		RandFloat(-2000, 2000),
		RandFloat(500, 1500)
	);

	bs.vel = Vec(
		RandFloat(-500, 500),
		RandFloat(-500, 500),
		RandFloat(200, 600)
	);

	bs.angVel = RandVec(
		Vec(-2, -2, -2),
		Vec(2, 2, 2)
	);

	arena->ball->SetState(bs);

	for (Car* car : arena->_cars) {
		CarState cs = {};

		cs.pos =
			bs.pos +
			Vec(
				RandFloat(-400, 400),
				RandFloat(-400, 400),
				RandFloat(-600, -200)
			);

		cs.pos.z = 17;

		Vec toBall =
			(bs.pos - cs.pos).Normalized();

		cs.rotMat = Angle::FromVec(toBall).ToRotMat();

		cs.vel = RandVec(
			Vec(-200, -200, 0),
			Vec(200, 200, 100)
		);

		cs.boost = RandFloat(60, 100);
		cs.isOnGround = true;

		car->SetState(cs);
	}
}


void R3maJStateSetter::SetWallPlay(Arena* arena) {
	float sideX =
		(RandFloat() > 0.5f) ? 1.f : -1.f;

	float ballZ = RandFloat(500, 1500);

	BallState bs = {};

	bs.pos = Vec(
		sideX * RandFloat(3800, 4050),
		RandFloat(-800, 800),
		ballZ
	);

	bs.vel = Vec(
		-sideX * RandFloat(200, 800),
		RandFloat(-200, 200),
		RandFloat(-100, 100)
	);

	arena->ball->SetState(bs);

	for (Car* car : arena->_cars) {
		CarState cs = {};

		cs.pos = Vec(
			sideX * RandFloat(3000, 3800),
			RandFloat(-400, 400),
			17
		);

		Vec toBall =
			(bs.pos - cs.pos).Normalized();

		cs.rotMat = Angle::FromVec(toBall).ToRotMat();
		cs.boost = RandFloat(50, 100);
		cs.isOnGround = true;

		car->SetState(cs);
	}
}


void R3maJStateSetter::SetDribblePractice(Arena* arena) {
	for (Car* car : arena->_cars) {
		CarState cs = {};

		float yOff =
			(car->team == Team::BLUE)
			? -1.f
			: 1.f;

		cs.pos = Vec(
			RandFloat(-600, 600),
			yOff * RandFloat(600, 1200),
			17
		);

		cs.vel = Vec(
			RandFloat(0, 800) * yOff * 0.5f,
			RandFloat(0, 800) * yOff,
			0
		);

		Angle facing =
			Angle(RandFloat(-0.5f, 0.5f), 0, 0);

		cs.rotMat = facing.ToRotMat();
		cs.boost = RandFloat(30, 80);
		cs.isOnGround = true;

		car->SetState(cs);
	}

	BallState bs = {};

	for (Car* car : arena->_cars) {
		CarState cs = car->GetState();

		Vec roofPos =
			cs.pos + cs.rotMat.up * 100;

		bs.pos = roofPos;

		bs.vel =
			cs.vel +
			Vec(
				RandFloat(-50, 50),
				RandFloat(-50, 50),
				0
			);

		break;
	}

	arena->ball->SetState(bs);
}
