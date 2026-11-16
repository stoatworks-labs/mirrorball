#include "Ball.h"

#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace mirrorball
{
namespace ball
{
Layout Build( double diameter, double facet )
{
	Layout layout;
	layout.radius = 0.5 * diameter;
	layout.facet  = facet;
	layout.pitch  = kPitchFactor * facet;
	layout.delta  = layout.pitch / layout.radius;
	const int rings = std::clamp( static_cast< int >( std::floor( kPi * layout.radius / layout.pitch ) ) - 1, 1, kMaxRings );
	layout.rings.resize( static_cast< size_t >( rings ) );
	layout.total = 0;
	for( int k = 0; k < rings; ++k )
	{
		Ring& ring  = layout.rings[ static_cast< size_t >( k ) ];
		ring.phi    = ( k - 0.5 * ( rings - 1 ) ) * layout.delta;
		ring.sinPhi = std::sin( ring.phi );
		ring.cosPhi = std::cos( ring.phi );
		ring.count  = std::max( 1, static_cast< int >( std::floor( 2.0 * kPi * layout.radius * ring.cosPhi / layout.pitch ) ) );
		//A fixed offset per ring, so the rings do not line up: hashed, and
		//not from the Seed (Seed only picks the missing facets).
		ring.offset = static_cast< double >( Pcg( static_cast< uint32_t >( k ) + 0x1B873593u ) & 0xFFFFu ) / 65536.0;
		layout.total += ring.count;
	}
	return layout;
}

uint32_t Pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

uint32_t FacetHash( int ring, int index, int seed )
{
	return Pcg( static_cast< uint32_t >( ring ) * 7919u
	            + Pcg( static_cast< uint32_t >( index ) + Pcg( static_cast< uint32_t >( seed ) + 0x68E31DA4u ) ) );
}

uint32_t MissingThreshold( double fraction )
{
	if( !( fraction > 0.0 ) )
		return 0;
	return static_cast< uint32_t >( std::min( fraction, 1.0 ) * 4294967295.0 );
}

double CosTurn( double turns )
{
	return std::cos( 2.0 * kPi * ( turns - std::floor( turns ) ) );
}
double SinTurn( double turns )
{
	return std::sin( 2.0 * kPi * ( turns - std::floor( turns ) ) );
}

Vec3 NormalOf( const Ring& ring, int index, double psi )
{
	const double t = ( index + ring.offset ) / ring.count + psi;
	return { ring.cosPhi * CosTurn( t ), ring.sinPhi, -ring.cosPhi * SinTurn( t ) };
}

void EdgesOf( const Ring& ring, int index, double psi, Vec3& east, Vec3& north )
{
	const double t = ( index + ring.offset ) / ring.count + psi;
	const double c = CosTurn( t ), s = SinTurn( t );
	east  = { -s, 0.0, -c };
	north = { -ring.sinPhi * c, ring.cosPhi, ring.sinPhi * s };
}

int NearestRing( const Layout& layout, double phi )
{
	const int k = static_cast< int >( std::lround( phi / layout.delta + 0.5 * ( layout.Rings() - 1 ) ) );
	return std::clamp( k, 0, layout.Rings() - 1 );
}

int NearestIndex( const Ring& ring, double theta, double psi )
{
	const double inBall = theta - psi;
	const long j        = std::lround( inBall * ring.count - ring.offset );
	const long n        = ring.count;
	return static_cast< int >( ( ( j % n ) + n ) % n );
}
} // namespace ball

namespace room
{
Room Of( double width )
{
	Room r;
	r.width  = width;
	r.height = 0.5 * width;
	r.min    = { -0.5 * width, -0.75 * r.height, -0.5 * width };
	r.max    = { 0.5 * width, 0.25 * r.height, 0.5 * width };
	return r;
}

View RoomCamera( const Room& room )
{
	View v;
	v.ortho    = false;
	v.eye      = { 0.0, room.min[ 1 ] + 1.7, room.max[ 2 ] - 0.5 };
	v.forward  = normalise( Vec3 { 0.0, 0.0, 0.0 } - v.eye );
	v.right    = normalise( cross( v.forward, Vec3 { 0.0, 1.0, 0.0 } ) );
	v.up       = cross( v.right, v.forward );
	v.tanHalfV = std::tan( 30.0 * kPi / 180.0 );
	return v;
}

View Plane( const Vec3& centre, const Vec3& normal, const Vec3& upHint, double halfHeight )
{
	View v;
	v.ortho      = true;
	v.centre     = centre;
	v.normal     = normalise( normal );
	v.right      = normalise( cross( upHint, v.normal ) );
	v.up         = cross( v.normal, v.right );
	v.halfHeight = halfHeight;
	return v;
}

View WallPlane( const Room& room )
{
	return Plane( { 0.0, 0.5 * ( room.min[ 1 ] + room.max[ 1 ] ), room.min[ 2 ] }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 },
	              0.5 * room.height );
}
} // namespace room

namespace lamp
{
double BeamLumens()
{
	const double intensity = kChauvetLux * kChauvetMetres * kChauvetMetres;
	return intensity * 2.0 * kPi * ( 1.0 - std::cos( 0.5 * kChauvetBeamDegrees * kPi / 180.0 ) );
}

void GoboFrame( const Vec3& axis, Vec3& right, Vec3& up )
{
	const Vec3 a = normalise( axis );
	Vec3 hint    = { 0.0, 1.0, 0.0 };
	if( std::fabs( a[ 1 ] ) > std::cos( 3.0 * kPi / 180.0 ) )
		hint = { 0.0, 0.0, -1.0 };
	up    = normalise( hint - dot( hint, a ) * a );
	right = cross( a, up );
}

Lamp At( const Vec3& position, double beamDegrees )
{
	Lamp l;
	l.position  = position;
	l.axis      = normalise( Vec3 { 0.0, 0.0, 0.0 } - position );
	GoboFrame( l.axis, l.right, l.up );
	l.alpha     = 0.5 * beamDegrees * kPi / 180.0;
	l.tanAlpha  = std::tan( l.alpha );
	l.intensity = BeamLumens() / ( 2.0 * kPi * ( 1.0 - std::cos( l.alpha ) ) );
	const double a = 0.5 * kPupilDiameter;
	l.luminance = l.intensity / ( kPi * a * a );
	l.throwUsed = length( position );
	return l;
}

Lamp Place( double throwMetres, double elevationDegrees, double bearingDegrees, double beamDegrees, const room::Room& room )
{
	const double e = elevationDegrees * kPi / 180.0, b = bearingDegrees * kPi / 180.0;
	const Vec3 dir = { std::cos( e ) * std::sin( b ), std::sin( e ), std::cos( e ) * std::cos( b ) };
	double t       = throwMetres;
	for( int i = 0; i < 3; ++i )
	{
		if( dir[ i ] > 1e-12 )
			t = std::min( t, ( room.max[ i ] - room::kLampMargin ) / dir[ i ] );
		else if( dir[ i ] < -1e-12 )
			t = std::min( t, ( room.min[ i ] + room::kLampMargin ) / dir[ i ] );
	}
	return At( t * dir, beamDegrees );
}
} // namespace lamp

} // namespace mirrorball
