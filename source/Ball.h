#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/**
    The ball, the lamp and the room: geometry in double, shared by the plugin
    (which sends it to the shader) and the harness (which builds its closed
    forms from it). Nothing here is physics the checks measure: it is the
    layout (where each facet is), the hash (which facets are missing), and
    where the lamp, the room and the camera are.

    Coordinates: metres, the ball's centre at the origin, x right, y up, z
    toward the camera. The motor turns the ball about +y; positive turns are
    anticlockwise seen from above (right-handed about +y).

    A facet at latitude phi and longitude theta (turns, the ball's frame)
    has the normal

        n = ( cos phi cos 2 pi t, sin phi, -cos phi sin 2 pi t ),
        t = theta + psi,

    psi the motor's phase in turns. Its centre is R n; its edges run east
    ( -sin 2 pi t, 0, -cos 2 pi t ) and north
    ( -sin phi cos 2 pi t, cos phi, sin phi sin 2 pi t ).
*/
namespace mirrorball
{
using Vec3 = std::array< double, 3 >;

inline Vec3 operator+( const Vec3& a, const Vec3& b )
{
	return { a[ 0 ] + b[ 0 ], a[ 1 ] + b[ 1 ], a[ 2 ] + b[ 2 ] };
}
inline Vec3 operator-( const Vec3& a, const Vec3& b )
{
	return { a[ 0 ] - b[ 0 ], a[ 1 ] - b[ 1 ], a[ 2 ] - b[ 2 ] };
}
inline Vec3 operator*( double s, const Vec3& a )
{
	return { s * a[ 0 ], s * a[ 1 ], s * a[ 2 ] };
}
inline double dot( const Vec3& a, const Vec3& b )
{
	return a[ 0 ] * b[ 0 ] + a[ 1 ] * b[ 1 ] + a[ 2 ] * b[ 2 ];
}
inline Vec3 cross( const Vec3& a, const Vec3& b )
{
	return { a[ 1 ] * b[ 2 ] - a[ 2 ] * b[ 1 ], a[ 2 ] * b[ 0 ] - a[ 0 ] * b[ 2 ], a[ 0 ] * b[ 1 ] - a[ 1 ] * b[ 0 ] };
}
inline double length( const Vec3& a )
{
	return std::sqrt( dot( a, a ) );
}
inline Vec3 normalise( const Vec3& a )
{
	const double l = length( a );
	return l > 0.0 ? ( 1.0 / l ) * a : Vec3 { 0.0, 0.0, 0.0 };
}
/// The law of reflection, PBRT 4ed Eq. 9.1 written for a direction of
/// travel: w - 2 ( w . n ) n.
inline Vec3 reflect( const Vec3& w, const Vec3& n )
{
	return w - ( 2.0 * dot( w, n ) ) * n;
}

namespace ball
{
/// Pitch over facet size: 10% grout, an assumption (SPEC-mirrorball.md).
constexpr double kPitchFactor = 1.1;
/// EN 1036's floor for silvered 2-6 mm clear float, via AGC's Mirox sheet.
constexpr double kReflectance = 0.86;
/// The ring table's width: the controls' ceiling is 284 rings (1 m, 5 mm).
constexpr int kMaxRings = 288;

struct Ring
{
	double phi    = 0.0;///< latitude, radians
	double sinPhi = 0.0;
	double cosPhi = 1.0;
	int count     = 1;  ///< facets in the ring
	double offset = 0.0;///< o_k, in facets: facet j sits at ( j + o_k ) / count turns
};

struct Layout
{
	double radius = 0.25;
	double facet  = 0.01;
	double pitch  = 0.011;
	double delta  = 0.044;///< ring spacing in latitude, p / R
	int total     = 0;
	std::vector< Ring > rings;

	int Rings() const
	{
		return static_cast< int >( rings.size() );
	}
};

/// Rings of equal latitude p / R apart, floor( pi R / p ) - 1 of them,
/// centred on the equator; ring k holds max( 1, floor( 2 pi R cos phi / p ) ).
Layout Build( double diameter, double facet );

/// The PCG output mix, bit for bit the shader's `pcg`.
uint32_t Pcg( uint32_t v );
/// The facet's hash: the shader's `facetHash`.
uint32_t FacetHash( int ring, int index, int seed );
/// A facet is missing when its hash is below this (0 when nothing is).
uint32_t MissingThreshold( double fraction );
inline bool IsMissing( int ring, int index, int seed, uint32_t threshold )
{
	return threshold > 0 && FacetHash( ring, index, seed ) < threshold;
}

double CosTurn( double turns );
double SinTurn( double turns );

/// The facet's world normal at motor phase psi (turns).
Vec3 NormalOf( const Ring& ring, int index, double psi );
/// Its east and north edges.
void EdgesOf( const Ring& ring, int index, double psi, Vec3& east, Vec3& north );

/// The ring whose latitude is nearest phi (clamped into the ball).
int NearestRing( const Layout& layout, double phi );
/// The facet of ring k nearest longitude theta (world turns) at phase psi.
int NearestIndex( const Ring& ring, double theta, double psi );
} // namespace ball

namespace room
{
/// Albedos: a pale hall with a dark floor (assumptions).
constexpr double kWallAlbedo    = 0.7;
constexpr double kCeilingAlbedo = 0.5;
constexpr double kFloorAlbedo   = 0.2;
/// The lamp stays this far inside the walls.
constexpr double kLampMargin = 0.2;

struct Room
{
	double width  = 12.0;
	double height = 6.0;
	Vec3 min {}, max {};///< the box, relative to the ball's centre
};

/// Room wide and deep, half as high, the ball 3/4 of the way up, centred.
Room Of( double width );

struct View
{
	bool ortho = false;
	//Perspective: the eye, the frame, tan of half the vertical field.
	Vec3 eye {}, forward {}, right {}, up {};
	double tanHalfV = 0.5;
	//Orthographic: the plane's centre, its frame, half its height.
	Vec3 centre {}, normal {};
	double halfHeight = 1.0;
};

/// The Room view: eye height 1.7 m, 0.5 m inside the front wall, aimed at
/// the ball, 60 degrees vertical.
View RoomCamera( const Room& room );
/// The Wall view: the back wall square on, floor to ceiling.
View WallPlane( const Room& room );
/// An orthographic plane anywhere (the harness's test plane): `normal`
/// faces the light, `upHint` sets the frame's up.
View Plane( const Vec3& centre, const Vec3& normal, const Vec3& upHint, double halfHeight );
} // namespace room

namespace lamp
{
/// The PAR36's diameter, as the exit pupil (GE 4515, Tungsram sheet).
constexpr double kPupilDiameter = 0.114;
/// Chauvet's LED Pinspot 2: 4,120 lux at 2 m with its 6 degree lens, so
/// I = 16,480 cd and the beam's flux 16,480 x 2 pi ( 1 - cos 3 deg ).
constexpr double kChauvetLux = 4120.0, kChauvetMetres = 2.0, kChauvetBeamDegrees = 6.0;
double BeamLumens();

struct Lamp
{
	Vec3 position {};
	Vec3 axis {}, right {}, up {};///< the gobo's frame: upright to someone behind the lamp
	double alpha     = 0.0;      ///< half the beam, radians
	double tanAlpha  = 0.0;
	double intensity = 0.0;///< I0, cd
	double luminance = 0.0;///< L_e = I0 / ( pi ( A / 2 )^2 ), cd/m^2
	double throwUsed = 0.0;///< the distance after the room's clamp
};

/// The lamp at `throwMetres` from the ball's centre, at elevation and
/// bearing (degrees), aimed at the centre, pulled in to stay inside the room.
Lamp Place( double throwMetres, double elevationDegrees, double bearingDegrees, double beamDegrees, const room::Room& room );
/// The lamp exactly at `position` (the harness), aimed at the ball's centre.
Lamp At( const Vec3& position, double beamDegrees );
/// The gobo's frame for an axis: U is world up made perpendicular to it (-z
/// within 3 degrees of vertical), R = axis x U.
void GoboFrame( const Vec3& axis, Vec3& right, Vec3& up );
} // namespace lamp

} // namespace mirrorball
