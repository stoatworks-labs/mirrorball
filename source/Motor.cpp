#include "Motor.h"

#include <algorithm>
#include <cmath>

namespace mirrorball
{
namespace
{
/// The raised cosine: 0 at the step's start, 1 at its end, no jerk at
/// either. In double: a step's phase must agree at every frame rate.
double ease( double x )
{
	if( x <= 0.0 )
		return 0.0;
	if( x >= 1.0 )
		return 1.0;
	return 0.5 - 0.5 * std::cos( kPi * x );
}
constexpr size_t kStepHistory = 64;
} // namespace

void MotorState::Reset()
{
	const int keep = hooks;
	*this          = MotorState();
	hooks          = keep;
}

void MotorState::fold( double turns )
{
	base += turns;
	const double whole = std::floor( base );
	wholeTurns += static_cast< int64_t >( whole );
	base -= whole;
}

void MotorState::Advance( double now, double dt, const MotorInputs& in )
{
	if( in.mode == Motor::Free )
	{
		fold( in.rpm / 60.0 * ( ( hooks & kMotorPerFrame ) ? 1.0 / 60.0 : dt ) );
		//The negative: the phase from the host's absolute clock in float,
		//which at ~499 million ms resolves 1/32 s.
		const float t = static_cast< float >( now ), rate = static_cast< float >( in.rpm / 60.0 );
		floatPhase    = static_cast< double >( t * rate - std::floor( t * rate ) );
		beatPos       = -1.0;
	}
	else
	{
		//Where the beat is: the host's bar phase once it has been seen to
		//move, otherwise the plugin's own clock at the last BPM.
		const double bpm = in.bpm > 0.0 && std::isfinite( in.bpm ) ? in.bpm : 120.0;
		double p0 = in.hostBeat == lastHost ? beatPos : -1.0, p1;
		lastHost  = in.hostBeat;
		if( in.hostBeat )
		{
			p1 = 4.0 * ( in.barPhase - std::floor( in.barPhase ) );
			if( p0 >= 0.0 && p1 + 2.0 < p0 )
				p1 += 4.0;//the bar wrapped inside the frame
		}
		else
		{
			if( p0 < 0.0 )
				ownBeats = 0.0;
			p0       = p0 < 0.0 ? -1.0 : ownBeats;
			ownBeats += bpm / 60.0 * dt;
			p1       = ownBeats;
		}

		const double step = BeatsOf( in.stepOn );
		if( p0 >= 0.0 && p1 > p0 )
		{
			//Every boundary crossed inside the frame, at its own instant.
			double boundary = ( std::floor( p0 / step + 1e-9 ) + 1.0 ) * step;
			while( boundary <= p1 + 1e-12 )
			{
				const double inside = ( boundary - p0 ) / ( p1 - p0 );
				double start        = ( now - dt ) + inside * dt;
				if( hooks & kMotorStepAtFrame )
					start = now;
				const double turns = ( in.rpm < 0.0 ? -1.0 : 1.0 ) * in.stepTurns;
				moves.push_back( { start, std::max( 0.0, in.glide ) * step * 60.0 / bpm, turns } );
				stepStarts.push_back( start );
				if( stepStarts.size() > kStepHistory )
					stepStarts.erase( stepStarts.begin() );
				++steps;
				boundary += step;
			}
		}
		if( in.hostBeat )
			beatPos = p1 - 4.0 * std::floor( p1 / 4.0 );
		else
			beatPos = ownBeats;
	}

	//The moves in flight at `now`; a finished one joins the settled phase.
	easing = 0.0;
	for( size_t i = 0; i < moves.size(); )
	{
		const Move& m = moves[ i ];
		if( m.duration <= 0.0 || now >= m.start + m.duration )
		{
			fold( m.turns );
			moves.erase( moves.begin() + static_cast< long >( i ) );
			continue;
		}
		easing += m.turns * ease( ( now - m.start ) / m.duration );
		++i;
	}
}

double MotorState::Phase() const
{
	if( hooks & kMotorFloatClock )
		return floatPhase;
	const double p = base + easing;
	return p - std::floor( p );
}

double MotorState::Unwrapped() const
{
	return static_cast< double >( wholeTurns ) + base + easing;
}

} // namespace mirrorball
