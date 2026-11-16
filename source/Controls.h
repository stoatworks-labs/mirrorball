#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1: `SetParamInfo` clamps an
    `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange` could widen
    it. The conversions live in Controls.cpp, one function per control, each
    with its inverse where a default or the harness needs one. An option
    parameter holds the element index, and reads back 0..1 whatever its
    count, so options are mapped by index. Seed is a real integer
    (`FF_TYPE_INTEGER`).

    Units: metres, millimetres for the facets, degrees for every angle,
    revolutions a minute for the motor, stops of exposure.
*/
namespace mirrorball
{
enum ParamId : unsigned int
{
	PT_DIAMETER = 0,
	PT_FACET_SIZE,
	PT_MISSING,
	PT_SEED,

	PT_MOTOR,
	PT_SPEED,
	PT_STEP_ON,
	PT_STEP_ANGLE,
	PT_GLIDE,

	PT_BEAM,
	PT_THROW,
	PT_ELEVATION,
	PT_BEARING,
	PT_GOBO,///< the source, and the Over in Surface
	PT_COLOUR,

	PT_ROOM,
	PT_VIEW,///< source only: the Over is always the wall
	PT_ROOM_LIGHT,
	PT_EXPOSURE,

	PT_CLIP_AS,///< Over only
	PT_MIX,    ///< Over only

	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Integer
};

struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, the element index, or the integer
	const char* const* options;
	int optionCount;
	float low, high;///< Integer only: the range
};

const ControlInfo& InfoOf( unsigned int id );

/// Host index -> ParamId for one plugin: the source has no Clip As or Mix,
/// the Over no View.
const std::vector< unsigned int >& HostOrder( bool effect );

/// An option's element index from whatever the host sent (index, rounded).
int OptionIndex( float value, int count );

enum class Motor
{
	Free = 0,///< turning at Speed
	Sync,    ///< stepping on the host's beat
	Count
};

enum class StepOn
{
	HalfBeat = 0,
	Beat,
	TwoBeats,
	Bar,
	Count
};

enum class Gobo
{
	Open = 0,
	Dots,
	Ring,
	Star,
	Bars,
	Count,
	Clip = 100///< not an option: the Over's Clip As Gobo
};

enum class Colour
{
	White = 0,
	Red,
	Amber,
	Green,
	Cyan,
	Blue,
	Magenta,
	Count
};

enum class View
{
	Room = 0,///< a camera in the room, looking at the ball
	Wall,    ///< the back wall, square on
	Count
};

enum class ClipAs
{
	Surface = 0,///< the clip is the wall; the spots light it
	Gobo,       ///< the clip is the pin spot's gobo
	Count
};

double DiameterFromParam( float value );///< metres
float ParamFromDiameter( double metres );

double FacetFromParam( float value );///< metres (the control reads millimetres)
float ParamFromFacet( double metres );

double MissingFromParam( float value );///< fraction 0..0.5
float ParamFromMissing( double fraction );

double SpeedFromParam( float value );///< rpm, signed
float ParamFromSpeed( double rpm );

double StepAngleFromParam( float value );///< degrees
float ParamFromStepAngle( double degrees );

double GlideFromParam( float value );///< fraction of the step interval

double BeamFromParam( float value );///< full beam angle, degrees
float ParamFromBeam( double degrees );

double ThrowFromParam( float value );///< metres, lamp to ball centre
float ParamFromThrow( double metres );

double ElevationFromParam( float value );///< degrees above the ball's horizon
float ParamFromElevation( double degrees );

double BearingFromParam( float value );///< degrees, 0 toward the camera, + to its right
float ParamFromBearing( double degrees );

double RoomFromParam( float value );///< metres, the room's width and depth
float ParamFromRoom( double metres );

double RoomLightFromParam( float value );///< fraction of white

double ExposureFromParam( float value );///< stops
float ParamFromExposure( double stops );

/// Step On's interval in beats.
double BeatsOf( StepOn on );

/// Linear-sRGB filter colours: chosen, not measured (SPEC-mirrorball.md).
void ColourOf( Colour colour, double rgb[ 3 ] );

constexpr double kPi = 3.14159265358979323846;

} // namespace mirrorball
