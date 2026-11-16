#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace mirrorball
{
namespace
{
double clamp01( float v )
{
	return std::isfinite( v ) ? std::clamp( static_cast< double >( v ), 0.0, 1.0 ) : 0.0;
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

//Steinigke/Eurolite balls run 20 cm to 100 cm; facets 5 mm and 10 mm are on
//their sheets, 20 mm is past them on purpose (SPEC-mirrorball.md, The ball).
constexpr double kDiameterLow = 0.2, kDiameterHigh = 1.0;
constexpr double kFacetLow = 0.005, kFacetHigh = 0.020;
constexpr double kMissingHigh = 0.5;
//The Stairville MBM40D turns 0.5-5 rpm; the MD-2010 runs both ways.
constexpr double kSpeedHigh = 5.0;
constexpr double kStepLow = 1.0, kStepHigh = 90.0;
//3 degrees (ADJ's lens) to 12 (its wide lens); Eurolite's PST-5 is 5.
constexpr double kBeamLow = 3.0, kBeamHigh = 12.0;
constexpr double kThrowLow = 0.5, kThrowHigh = 10.0;
constexpr double kElevationHigh = 90.0;
constexpr double kBearingHigh = 180.0;
constexpr double kRoomLow = 6.0, kRoomHigh = 30.0;
constexpr double kStops = 4.0;

const char* const kMotorNames[]  = { "Free", "Sync" };
const char* const kStepOnNames[] = { "Half Beat", "Beat", "2 Beats", "Bar" };
const char* const kGoboNames[]   = { "Open", "Dots", "Ring", "Star", "Bars" };
const char* const kColourNames[] = { "White", "Red", "Amber", "Green", "Cyan", "Blue", "Magenta" };
const char* const kViewNames[]   = { "Room", "Wall" };
const char* const kClipAsNames[] = { "Surface", "Gobo" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0, 0.0f, 1.0f };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count, 0.0f, 1.0f };
}
ControlInfo integer( unsigned int id, const char* name, const char* group, float value, float low, float high )
{
	return { id, name, group, Kind::Integer, value, nullptr, 0, low, high };
}

//---------------------------------------------------------------------------
// The defaults: a 50 cm ball of 10 mm glass, turning at 1 rpm (the
// MD-2010's one speed), a 5 degree pin spot 5 m away just filling it, low
// on the right, in a 12 m hall. AGENTS.md ("Decisions") says how each was
// chosen. The Over shares them all but Exposure, which was set on Resolume's
// demo clips (DefaultOf, below).
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	standard( PT_DIAMETER, "Diameter", "Ball", ParamFromDiameter( 0.5 ) ),
	standard( PT_FACET_SIZE, "Facet Size", "Ball", ParamFromFacet( 0.010 ) ),
	standard( PT_MISSING, "Missing", "Ball", 0.0f ),
	integer( PT_SEED, "Seed", "Ball", 1.0f, 0.0f, 999.0f ),

	option( PT_MOTOR, "Motor", "Motor", 0.0f, kMotorNames, static_cast< int >( Motor::Count ) ),
	standard( PT_SPEED, "Speed", "Motor", ParamFromSpeed( 1.0 ) ),
	option( PT_STEP_ON, "Step On", "Motor", 1.0f, kStepOnNames, static_cast< int >( StepOn::Count ) ),
	standard( PT_STEP_ANGLE, "Step Angle", "Motor", ParamFromStepAngle( 15.0 ) ),
	standard( PT_GLIDE, "Glide", "Motor", 0.5f ),

	standard( PT_BEAM, "Beam", "Spot", ParamFromBeam( 5.0 ) ),
	standard( PT_THROW, "Throw", "Spot", ParamFromThrow( 5.0 ) ),
	standard( PT_ELEVATION, "Elevation", "Spot", ParamFromElevation( 10.0 ) ),
	standard( PT_BEARING, "Bearing", "Spot", ParamFromBearing( 75.0 ) ),
	option( PT_GOBO, "Gobo", "Spot", 0.0f, kGoboNames, static_cast< int >( Gobo::Count ) ),
	option( PT_COLOUR, "Colour", "Spot", 0.0f, kColourNames, static_cast< int >( Colour::Count ) ),

	standard( PT_ROOM, "Room", "Room", ParamFromRoom( 12.0 ) ),
	option( PT_VIEW, "View", "Room", 0.0f, kViewNames, static_cast< int >( View::Count ) ),
	standard( PT_ROOM_LIGHT, "Room Light", "Room", 0.03f ),
	standard( PT_EXPOSURE, "Exposure", "Room", ParamFromExposure( 0.0 ) ),

	option( PT_CLIP_AS, "Clip As", "Over", 0.0f, kClipAsNames, static_cast< int >( ClipAs::Count ) ),
	standard( PT_MIX, "Mix", "Over", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

//The Over's Exposure: the largest, in sixth-stop steps, at which the spots
//blow out (drive to white a channel the clip had below 250) no more than 1%
//of the spot-lit pixels of the median demo clip. tools/clips.py defaults
//measures it and fails when this is not what the clips give.
constexpr double kOverExposureStops = 5.0 / 6.0;

float DefaultOf( unsigned int id, bool effect )
{
	if( effect && id == PT_EXPOSURE )
		return ParamFromExposure( kOverExposureStops );
	return InfoOf( id ).defaultValue;
}

const std::vector< unsigned int >& HostOrder( bool effect )
{
	auto build = []( bool over ) {
		std::vector< unsigned int > order;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			const bool overOnly   = id == PT_CLIP_AS || id == PT_MIX;
			const bool sourceOnly = id == PT_VIEW;
			if( ( !over && overOnly ) || ( over && sourceOnly ) )
				continue;
			order.push_back( id );
		}
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	};
	static const std::vector< unsigned int > source = build( false ), over = build( true );
	return effect ? over : source;
}

int OptionIndex( float value, int count )
{
	if( !std::isfinite( value ) )
		return 0;
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double DiameterFromParam( float v )
{
	return linear( v, kDiameterLow, kDiameterHigh );
}
float ParamFromDiameter( double metres )
{
	return inverseLinear( metres, kDiameterLow, kDiameterHigh );
}

double FacetFromParam( float v )
{
	return geometric( v, kFacetLow, kFacetHigh );
}
float ParamFromFacet( double metres )
{
	return inverseGeometric( metres, kFacetLow, kFacetHigh );
}

double MissingFromParam( float v )
{
	return linear( v, 0.0, kMissingHigh );
}
float ParamFromMissing( double fraction )
{
	return inverseLinear( fraction, 0.0, kMissingHigh );
}

double SpeedFromParam( float v )
{
	return linear( v, -kSpeedHigh, kSpeedHigh );
}
float ParamFromSpeed( double rpm )
{
	return inverseLinear( rpm, -kSpeedHigh, kSpeedHigh );
}

double StepAngleFromParam( float v )
{
	return geometric( v, kStepLow, kStepHigh );
}
float ParamFromStepAngle( double degrees )
{
	return inverseGeometric( degrees, kStepLow, kStepHigh );
}

double GlideFromParam( float v )
{
	return clamp01( v );
}

double BeamFromParam( float v )
{
	return linear( v, kBeamLow, kBeamHigh );
}
float ParamFromBeam( double degrees )
{
	return inverseLinear( degrees, kBeamLow, kBeamHigh );
}

double ThrowFromParam( float v )
{
	return geometric( v, kThrowLow, kThrowHigh );
}
float ParamFromThrow( double metres )
{
	return inverseGeometric( metres, kThrowLow, kThrowHigh );
}

double ElevationFromParam( float v )
{
	return linear( v, -kElevationHigh, kElevationHigh );
}
float ParamFromElevation( double degrees )
{
	return inverseLinear( degrees, -kElevationHigh, kElevationHigh );
}

double BearingFromParam( float v )
{
	return linear( v, -kBearingHigh, kBearingHigh );
}
float ParamFromBearing( double degrees )
{
	return inverseLinear( degrees, -kBearingHigh, kBearingHigh );
}

double RoomFromParam( float v )
{
	return linear( v, kRoomLow, kRoomHigh );
}
float ParamFromRoom( double metres )
{
	return inverseLinear( metres, kRoomLow, kRoomHigh );
}

double RoomLightFromParam( float v )
{
	return clamp01( v );
}

double ExposureFromParam( float v )
{
	return linear( v, -kStops, kStops );
}
float ParamFromExposure( double stops )
{
	return inverseLinear( stops, -kStops, kStops );
}

double BeatsOf( StepOn on )
{
	switch( on )
	{
	case StepOn::HalfBeat: return 0.5;
	case StepOn::Beat: return 1.0;
	case StepOn::TwoBeats: return 2.0;
	case StepOn::Bar: return 4.0;
	default: return 1.0;
	}
}

void ColourOf( Colour colour, double rgb[ 3 ] )
{
	static const double table[][ 3 ] = {
		{ 1.0, 1.0, 1.0 },  //White
		{ 1.0, 0.02, 0.01 },//Red
		{ 1.0, 0.45, 0.04 },//Amber
		{ 0.05, 1.0, 0.08 },//Green
		{ 0.04, 0.85, 1.0 },//Cyan
		{ 0.03, 0.12, 1.0 },//Blue
		{ 1.0, 0.04, 0.75 },//Magenta
	};
	const int i = std::clamp( static_cast< int >( colour ), 0, static_cast< int >( Colour::Count ) - 1 );
	for( int k = 0; k < 3; ++k )
		rgb[ k ] = table[ i ][ k ];
}

} // namespace mirrorball
