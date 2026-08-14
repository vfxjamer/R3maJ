#include "R3maJRewards.h"
#include <algorithm>
#include <cmath>
namespace { float Clamp01(float x){return RS_CLAMP(x,0.f,1.f);} Vec OppGoal(const Player&p){return p.team==Team::BLUE?CommonValues::ORANGE_GOAL_BACK:CommonValues::BLUE_GOAL_BACK;} Vec OwnGoal(const Player&p){return p.team==Team::BLUE?CommonValues::BLUE_GOAL_BACK:CommonValues::ORANGE_GOAL_BACK;} }
float TouchReward::GetReward(const Player&p,const GameState&,bool){return p.ballTouchedStep?1.f:0.f;}
float SpeedToBallReward::GetReward(const Player&p,const GameState&s,bool){Vec d=s.ball.pos-p.pos;if(d.Length()<1.f)return 0.f;return p.vel.Dot(d.Normalized())/CommonValues::CAR_MAX_SPEED;}
float FaceBallReward::GetReward(const Player&p,const GameState&s,bool){Vec d=s.ball.pos-p.pos;if(d.Length()<1.f)return 0.f;return p.rotMat.forward.Dot(d.Normalized());}
float AirTimeReward::GetReward(const Player&p,const GameState&,bool){return p.isOnGround?0.f:1.f;}
float GroundIdlePenalty::GetReward(const Player&p,const GameState&s,bool){if(!p.isOnGround||p.vel.Length()>300.f||(s.ball.pos-p.pos).Length()<1500.f)return 0.f;return -1.f;}
float BallToGoalReward::GetReward(const Player&p,const GameState&s,bool){Vec d=OppGoal(p)-s.ball.pos;if(d.Length()<1.f)return 0.f;return RS_CLAMP(s.ball.vel.Dot(d.Normalized())/CommonValues::BALL_MAX_SPEED,-1.f,1.f);}
float TouchAccelerationReward::GetReward(const Player&p,const GameState&s,bool){if(!p.ballTouchedStep||!s.prev)return 0.f;return Clamp01((s.ball.vel-s.prev->ball.vel).Length()/CommonValues::BALL_MAX_SPEED);}
float TouchHeightReward::GetReward(const Player&p,const GameState&s,bool){if(!p.ballTouchedStep)return 0.f;return Clamp01((s.ball.pos.z-CommonValues::BALL_RADIUS)/(CommonValues::CEILING_Z-CommonValues::BALL_RADIUS));}
float SaveReward::GetReward(const Player&p,const GameState&s,bool){if(!p.ballTouchedStep||!s.prev)return 0.f;Vec d=OwnGoal(p)-s.ball.pos;if(d.Length()<1.f)return 0.f;d=d.Normalized();float before=s.prev->ball.vel.Dot(d)/CommonValues::BALL_MAX_SPEED;float after=s.ball.vel.Dot(d)/CommonValues::BALL_MAX_SPEED;return before>0.05f?Clamp01(before-after):0.f;}
float SaveBoostReward::GetReward(const Player&p,const GameState&,bool){return std::sqrt(Clamp01(p.boost/100.f));}
float BoostWastePenalty::GetReward(const Player&p,const GameState&,bool){if(!p.prev||!p.isOnGround)return 0.f;float d=Clamp01(p.boost/100.f)-Clamp01(p.prev->boost/100.f);return d<0.f?d:0.f;}
float AirTouchReward::GetReward(const Player&p,const GameState&s,bool){if(!p.ballTouchedStep||p.isOnGround)return 0.f;float air=Clamp01(p.airTimeSinceJump/1.75f);float h=Clamp01((s.ball.pos.z-150.f)/CommonValues::CEILING_Z);return std::min(air,h);}
float FlipResetReward::GetReward(const Player&p,const GameState&s,bool){if(!p.prev)return 0.f;if(!p.HasFlipOrJump()||p.prev->HasFlipOrJump()||p.pos.z<=3.f*CommonValues::BALL_RADIUS)return 0.f;Vec r=s.ball.pos-p.pos;if(r.Length()>2.f*CommonValues::BALL_RADIUS||r.Length()<1.f)return 0.f;return r.Normalized().Dot(-p.rotMat.up)>.9f?1.f:0.f;}
float WallTouchReward::GetReward(const Player&p,const GameState&s,bool){if(!p.ballTouchedStep)return 0.f;float wall=std::min(CommonValues::SIDE_WALL_X-std::fabs(p.pos.x),CommonValues::BACK_WALL_Y-std::fabs(p.pos.y));if(wall>300.f)return 0.f;return Clamp01((s.ball.pos.z-150.f)/CommonValues::CEILING_Z);}
void AirDribbleReward::Reset(const GameState&){_streak.clear();}
float AirDribbleReward::GetReward(const Player&p,const GameState&s,bool){if(p.isOnGround){_streak[p.carId]=0;return 0.f;}if(!p.ballTouchedStep)return 0.f;if((s.ball.pos-p.pos).Length()>2.f*CommonValues::BALL_RADIUS){_streak[p.carId]=0;return 0.f;}int&n=_streak[p.carId];n=std::min(n+1,8);return(float)n/8.f;}
float CradleReward::GetReward(const Player&p,const GameState&s,bool){if(!p.isOnGround)return 0.f;Vec r=s.ball.pos-p.pos;float h=std::sqrt(r.x*r.x+r.y*r.y);if(h>150.f||r.z<80.f||r.z>450.f)return 0.f;return Clamp01(1.f-h/150.f)*Clamp01(1.f-std::fabs(r.z-220.f)/220.f);}
float AngularMovementReward::GetReward(const Player&p,const GameState&,bool){return Clamp01(p.angVel.Length()/CommonValues::CAR_MAX_ANG_VEL);}
