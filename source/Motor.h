#pragma once

#include "Controls.h"

#include <cstdint>
#include <vector>

/**
    The motor: the ball's phase psi in turns, in double, on the CPU.

    Free turns at Speed: psi += rpm / 60 * dt, dt the frame's own step (time
    relative to the frame, never the host's absolute clock, which overflows a
    float at Resolume's ~499 million ms). Sync steps Step Angle on every Step
    On boundary of the host's beat: the boundary's instant is found inside
    the frame by interpolating the bar phase across it (ferrofluid's Drive),
    and the move eases by a raised cosine over Glide x the step interval from
    that instant. So the ball is in the same place at the same time at any
    frame rate.

    The phase is kept as a whole number of turns plus a fraction in [0, 1),
    so it never loses precision however long the ball runs.
*/
namespace mirrorball
{
struct MotorInputs
{
	Motor mode       = Motor::Free;
	double rpm       = 1.0;
	StepOn stepOn    = StepOn::Beat;
	double stepTurns = 15.0 / 360.0;
	double glide     = 0.5;
	double bpm       = 120.0;
	double barPhase  = 0.0;///< 0..1 across a 4-beat bar
	bool hostBeat    = false;///< the host's phase has been seen to move
};

/// Negative controls (the harness's): each breaks one thing a check measures.
enum MotorHook : int
{
	kMotorFloatClock  = 1 << 0,///< psi from the host's absolute time in float
	kMotorStepAtFrame = 1 << 1,///< a step starts at the frame, not at the beat inside it
	kMotorPerFrame    = 1 << 2 ///< Free advances a fixed 1/60 s a frame, whatever the frame's length
};

class MotorState
{
public:
	/// One frame: the host's time is `now` (s), the frame's step `dt`.
	void Advance( double now, double dt, const MotorInputs& in );
	void Reset();

	/// psi in [0, 1).
	double Phase() const;
	/// Whole turns plus the fraction: for the checks.
	double Unwrapped() const;
	/// Every step's start, s, as the motor scheduled it (the last 64).
	const std::vector< double >& StepStarts() const
	{
		return stepStarts;
	}
	int Steps() const
	{
		return steps;
	}
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}

private:
	struct Move
	{
		double start, duration, turns;
	};
	void fold( double now );

	int64_t wholeTurns = 0;
	double base        = 0.0;///< the settled phase's fraction, [0, 1)
	double easing      = 0.0;///< the moves in flight, turns
	double floatPhase  = 0.0;
	std::vector< Move > moves;
	std::vector< double > stepStarts;
	int steps          = 0;
	double beatPos     = -1.0;///< beats into the bar at the last frame
	double ownBeats    = 0.0; ///< the own clock, beats
	bool lastHost      = false;
	int hooks          = 0;
};

} // namespace mirrorball
