#include "Shaders.h"

namespace mirrorball::shaders
{

const char* const kVersion = "#version 410 core\n";

const char* const kQuadVertex = R"(
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//---------------------------------------------------------------------------
// Shared by every pass.
//---------------------------------------------------------------------------
const char* const kCommon = R"(
const float kPi = 3.14159265358979;

const int kHookRaw           = 1;
const int kHookCoverage      = 2;
const int kHookSpotsOnly     = 4;
const int kHookCount         = 8;
const int kHookGoboOut       = 16;
const int kHookNormalSpot    = 32;
const int kHookNoFactor2     = 64;
const int kHookVirtualApex   = 128;
const int kHookNoWindow      = 256;
const int kHookNoFacetCos    = 512;
const int kHookNoInvSquare   = 1024;
const int kHookDifferential  = 2048;
const int kHookUnmirrored    = 4096;
const int kHookAlphaThrough  = 8192;
const int kHookAlphaUnmasked = 16384;
const int kHookSaltByFrame   = 32768;

uniform int Hooks;

bool hooked( int bit )
{
	return ( Hooks & bit ) != 0;
}

//The PCG output mix: integer, so every GPU picks the same facets. Ball.cpp's
//Pcg is the same function, bit for bit.
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

uint facetHash( int ring, int index, int seed )
{
	return pcg( uint( ring ) * 7919u + pcg( uint( index ) + pcg( uint( seed ) + 0x68E31DA4u ) ) );
}

//cos( 2 pi t ), t in turns, without the built-in: reduced to [0, pi/2],
//then Taylor to a^14, whose remainder there is under 1e-10 (throw's).
float cosTurn( float t )
{
	float x = t - floor( t );
	x       = min( x, 1.0 - x );
	float flip = 1.0;
	if( x > 0.25 )
	{
		x    = 0.5 - x;
		flip = -1.0;
	}
	float a  = 6.28318530717958648 * x;
	float a2 = a * a;
	return flip * ( 1.0 + a2 * ( -0.5 + a2 * ( 1.0 / 24.0 + a2 * ( -1.0 / 720.0 + a2 * ( 1.0 / 40320.0
	       + a2 * ( -1.0 / 3628800.0 + a2 * ( 1.0 / 479001600.0 + a2 * ( -1.0 / 87178291200.0 ) ) ) ) ) ) ) );
}
float sinTurn( float t )
{
	return cosTurn( t - 0.25 );
}

//atan on [0, 1]: Abramowitz & Stegun 4.4.49, |error| <= 2e-8 rad. It only
//ever picks which facet is nearest; the gather's window absorbs its error.
float atanUnit( float z )
{
	float z2 = z * z;
	return z * ( 0.9999993329 + z2 * ( -0.3332985605 + z2 * ( 0.1994653599 + z2 * ( -0.1390853351
	       + z2 * ( 0.0964200441 + z2 * ( -0.0559098861 + z2 * ( 0.0218612288 - 0.0040540580 * z2 ) ) ) ) ) ) );
}
float atan2Rad( float y, float x )
{
	float ax = abs( x ), ay = abs( y );
	float m  = max( ax, ay );
	if( m <= 0.0 )
		return 0.0;
	float a = atanUnit( min( ax, ay ) / m );
	if( ay > ax )
		a = 0.5 * kPi - a;
	if( x < 0.0 )
		a = kPi - a;
	return y < 0.0 ? -a : a;
}
float atan2Turn( float y, float x )
{
	return atan2Rad( y, x ) / ( 2.0 * kPi );
}

//sRGB, both ways: light adds and multiplies in linear, the host sees code.
vec3 toLinear( vec3 code )
{
	vec3 c = max( code, vec3( 0.0 ) );
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 light )
{
	vec3 l = max( light, vec3( 0.0 ) );
	return mix( l * 12.92, 1.055 * pow( l, vec3( 1.0 / 2.4 ) ) - 0.055, step( vec3( 0.0031308 ), l ) );
}
)";

//---------------------------------------------------------------------------
// gobo: the transmission at g, |g| <= 1 the gate, in the central 256 x 256.
//---------------------------------------------------------------------------
const char* const kGoboFragment = R"(
uniform int GoboKind;//0 Open, 1 Dots, 2 Ring, 3 Star, 4 Bars, 100 the clip
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform float ClipAspect;

out vec4 fragColour;

const vec2 kDots[ 6 ] = vec2[ 6 ]( vec2( 0.58, 0.0 ), vec2( 0.29, 0.50229 ), vec2( -0.29, 0.50229 ),
                                   vec2( -0.58, 0.0 ), vec2( -0.29, -0.50229 ), vec2( 0.29, -0.50229 ) );

//The clip at uv (0..1 of its picture), linear and premultiplied, filtered
//by hand: the host's bilinear filter would blend a transparent texel's
//colour into its neighbours before the alpha could hide it.
vec3 clipAt( vec2 uv )
{
	ivec2 size = textureSize( InputTexture, 0 );
	ivec2 last = max( ivec2( vec2( size ) * MaxUV + 0.5 ) - 1, ivec2( 0 ) );
	vec2 p     = uv * MaxUV * vec2( size ) - 0.5;
	ivec2 i0   = ivec2( floor( p ) );
	vec2 f     = p - vec2( i0 );
	vec3 sum   = vec3( 0.0 );
	for( int k = 0; k < 4; ++k )
	{
		ivec2 o  = ivec2( k & 1, k >> 1 );
		vec4 t   = texelFetch( InputTexture, clamp( i0 + o, ivec2( 0 ), last ), 0 );
		float wx = o.x == 1 ? f.x : 1.0 - f.x;
		float wy = o.y == 1 ? f.y : 1.0 - f.y;
		sum += ( wx * wy * clamp( t.a, 0.0, 1.0 ) ) * toLinear( clamp( t.rgb, 0.0, 1.0 ) );
	}
	return sum;
}

vec3 pattern( vec2 g )
{
	//No gate here: the round gate is the final pass's analytic edge (its
	//ramp conserves the light), so a pattern runs on past |g| = 1 and the
	//mipmaps never pull black in across the gate.
	float rho = length( g );
	if( GoboKind == 100 )
	{
		//The clip, centre-cropped to a square (|g| <= 1 is the crop's
		//inscribed disc), its edge rows held beyond the frame, premultiplied:
		//transparent is no light.
		vec2 crop = ClipAspect >= 1.0 ? vec2( 0.5 / ClipAspect, 0.5 ) : vec2( 0.5, 0.5 * ClipAspect );
		return clipAt( clamp( vec2( 0.5 ) + g * crop, 0.0, 1.0 ) );
	}
	if( GoboKind == 1 )
	{
		float best = rho;
		for( int k = 0; k < 6; ++k )
			best = min( best, length( g - kDots[ k ] ) );
		return vec3( step( best, 0.2 ) );
	}
	if( GoboKind == 2 )
		return vec3( ( rho > 0.55 && rho < 0.8 ) || rho < 0.2 ? 1.0 : 0.0 );
	if( GoboKind == 3 )
	{
		float a = atan2Turn( g.y, g.x ) + 0.25;
		float f = fract( a * 5.0 );
		float s = abs( f - 0.5 ) * 2.0;
		return vec3( step( rho, 0.38 + 0.57 * s * s ) );
	}
	if( GoboKind == 4 )
		return vec3( step( fract( g.x * 2.5 + 0.25 ), 0.5 ) );
	return vec3( 1.0 );
}

void main()
{
	vec3 sum = vec3( 0.0 );
	for( int k = 0; k < 4; ++k )
	{
		vec2 offs = vec2( float( k & 1 ), float( k >> 1 ) ) * 0.5 - 0.25;
		vec2 g    = ( ( gl_FragCoord.xy + offs ) / 512.0 - 0.5 ) * 4.0;
		sum += pattern( g );
	}
	fragColour = vec4( sum * 0.25, 1.0 );
}
)";

//---------------------------------------------------------------------------
// final: the surface, the gather, the spill, the ball, the composite.
//---------------------------------------------------------------------------
const char* const kFinalFragment = R"(
uniform int ViewMode;//0 the room camera, 1 an orthographic plane (the wall, a test plane)
uniform vec3 Eye;
uniform vec3 Forward;
uniform vec3 Right;
uniform vec3 Up;
uniform vec2 TanHalf;//horizontal, vertical
uniform vec3 PlaneCentre;
uniform vec3 PlaneRight;
uniform vec3 PlaneUp;
uniform vec3 PlaneNormal;
uniform vec2 PlaneHalf;
uniform vec3 BoxMin;
uniform vec3 BoxMax;
uniform float WallAlbedo;
uniform float CeilingAlbedo;
uniform float FloorAlbedo;

uniform float BallRadius;
uniform float FacetSize;
uniform float Delta;//ring spacing, radians
uniform int Rings;
uniform sampler2D RingTable;//per ring: sin phi, cos phi, N, offset
uniform float Psi;          //the motor's phase, turns, in [0, 1)
uniform uint MissingBelow;
uniform int Seed;
uniform int FrameSalt;
uniform ivec2 Solo;
uniform int KMax;

uniform vec3 LampPos;
uniform vec3 LampAxis;
uniform vec3 LampRight;
uniform vec3 LampUp;
uniform float TanAlpha;
uniform float Alpha;
uniform float PupilRadius;
uniform vec3 Luminance;//rho_m L_e x colour, cd/m^2
uniform vec3 Intensity;//I0 x colour, cd
uniform float Reflectance;
uniform sampler2D Gobo;

uniform float Gain;//2^Exposure / E_ref
uniform float RoomLight;
uniform int IsOver;
uniform int ClipAs;//0 Surface, 1 Gobo
uniform sampler2D InputTexture;
uniform float MixAmount;
uniform ivec4 Viewport;

out vec4 fragColour;

const float kTexels = 128.0;//gobo texels per unit of g

//Where to centre a linear ramp of half-width w on the edge of a disc of
//radius r so the ramped disc holds exactly the disc's area: a ramp centred
//on r adds pi w^2 / 3 (SPEC-mirrorball.md, As built). Below r^2 = 4 w^2 / 3
//the ramp never reaches 1 and the area is pi ( r_c + w )^3 / ( 6 w ).
float rampEdge( float r, float w )
{
	float r2 = r * r, w2 = w * w;
	if( 3.0 * r2 >= 4.0 * w2 )
		return sqrt( r2 - w2 / 3.0 );
	return pow( 6.0 * w * r2, 1.0 / 3.0 ) - w;
}

vec3 goboAt( vec2 g, float footprint )
{
	float lod = log2( max( footprint * kTexels, 1.0 ) );
	return textureLod( Gobo, vec2( 0.5 ) + 0.25 * g, lod ).rgb;
}

int wrapIndex( int j, int n )
{
	return j - n * int( floor( float( j ) / float( n ) ) );
}

//---------------------------------------------------------------------------
// The gather: every facet that could send P light, traced back through its
// facet to the pin spot's pupil and gate (SPEC-mirrorball.md, Reflection).
//---------------------------------------------------------------------------
vec3 gather( vec3 P, vec3 NP, float foot, out float covSum, out float visited, out vec3 lastGobo, out float lastCov )
{
	vec3 sum = vec3( 0.0 );
	covSum   = 0.0;
	visited  = 0.0;
	lastGobo = vec3( 0.0 );
	lastCov  = 0.0;
	float R  = BallRadius;
	if( length( P ) < 1.05 * R )
		return sum;

	//The ideal normal: halfway between the way to P and the way to the
	//pupil's centre, refined once from where that facet actually sits.
	bool odd  = hooked( kHookNormalSpot ) || hooked( kHookNoFactor2 ) || hooked( kHookVirtualApex ) || hooked( kHookNoWindow );
	vec3 ns   = hooked( kHookNormalSpot ) ? normalize( P ) : normalize( normalize( P ) + normalize( LampPos ) );
	vec3 X1   = R * ns;
	vec3 uP   = normalize( P - X1 );
	vec3 vF   = normalize( LampPos - X1 );
	ns        = hooked( kHookNormalSpot ) ? uP : normalize( uP + vF );
	X1        = R * ns;
	float DP  = length( P - X1 );
	float DF  = length( LampPos - X1 );
	vF        = ( LampPos - X1 ) / DF;
	float cosI = dot( ns, vF );

	//The window: how far, in normal direction, a facet can stray and still
	//light P. The pupil's angular radius or the gate (whichever is
	//narrower), plus the facet and the pixel; halved by reflection's angle
	//doubling, widened where facets graze.
	float betaP    = PupilRadius / DF;
	float thetaOff = atan2Rad( length( cross( vF, -LampAxis ) ), dot( vF, -LampAxis ) );
	float rhoR     = ( odd ? Alpha + thetaOff + betaP : min( betaP, Alpha + thetaOff ) ) + ( FacetSize + foot ) / DP;
	float rhoN     = rhoR / ( 2.0 * max( cosI, 0.2 ) );
	int K          = min( int( ceil( rhoN / Delta ) ) + 1, KMax );

	float lat = atan2Rad( ns.y, length( ns.xz ) );
	int kc    = int( floor( lat / Delta + 0.5 * float( Rings - 1 ) + 0.5 ) );
	float thw = atan2Turn( -ns.z, ns.x ) - Psi;
	int salt  = hooked( kHookSaltByFrame ) ? FrameSalt : 0;

	for( int dk = -K; dk <= K; ++dk )
	{
		int k = kc + dk;
		if( k < 0 || k >= Rings )
			continue;
		vec4 ring = texelFetch( RingTable, ivec2( k, 0 ), 0 );
		int Nk    = int( ring.z + 0.5 );
		int cnt   = min( 2 * K + 1, Nk );
		int jc    = int( floor( thw * float( Nk ) - ring.w + 0.5 ) );
		int j0    = jc - ( cnt - 1 ) / 2;
		for( int i = 0; i < cnt; ++i )
		{
			int j = wrapIndex( j0 + i, Nk );
			visited += 1.0;
			if( Solo.x >= 0 && ( k != Solo.x || j != Solo.y ) )
				continue;
			if( Solo.x < -1 )
				continue;
			if( MissingBelow > 0u && facetHash( k, j, Seed + salt ) < MissingBelow )
				continue;

			float th = ( float( j ) + ring.w ) / float( Nk ) + ( hooked( kHookDifferential ) ? Psi * ring.y : Psi );
			float ct = cosTurn( th ), st = sinTurn( th );
			vec3 n   = vec3( ring.y * ct, ring.x, -ring.y * st );
			vec3 X   = R * n;
			vec3 d   = P - X;
			float D  = length( d );
			vec3 u   = d / D;
			float un = dot( u, n );
			if( un <= 0.0 )
				continue;

			//The light that leaves along u arrived along w = u - 2 ( u . n ) n.
			vec3 w;
			if( hooked( kHookNoFactor2 ) )
			{
				w = u - un * n;
				if( length( w ) < 1e-4 )
					continue;
				w = normalize( w );
			}
			else if( hooked( kHookNormalSpot ) )
				w = normalize( normalize( X - LampPos ) + ( u - n ) );
			else
				w = u - 2.0 * un * n;

			float den = dot( w, LampAxis );
			if( den <= 1e-4 )
				continue;
			//Back to the pupil's plane: is the ray inside the pupil, and
			//inside the gate?
			float ts = dot( X - LampPos, LampAxis ) / den;
			if( ts <= 0.0 )
				continue;
			float q = length( X - ts * w - LampPos );
			vec2 g  = vec2( dot( w, LampRight ), dot( w, LampUp ) ) / ( den * TanAlpha );

			//P sees the facet as a window of rays: the square projected from
			//P's mirror image, w_p wide on the pupil's plane and w_g in gobo
			//units, plus half the pixel. Linear ramps across them.
			float wp = 0.5 * FacetSize * ( D + ts ) / D + 0.5 * foot * ts / D;
			float wg = 0.5 * ( FacetSize + foot ) / ( D * TanAlpha );
			float cp = clamp( ( rampEdge( PupilRadius, wp ) - q ) / ( 2.0 * wp ) + 0.5, 0.0, 1.0 );
			if( hooked( kHookVirtualApex ) )
			{
				//The negative: rays from the lamp's mirror image, a point L
				//behind the facet.
				vec3 Fv  = LampPos - 2.0 * dot( LampPos - X, n ) * n;
				vec3 wv  = normalize( P - Fv );
				wv       = wv - 2.0 * dot( wv, n ) * n;
				float dv = dot( wv, LampAxis );
				if( dv <= 1e-4 )
					continue;
				g  = vec2( dot( wv, LampRight ), dot( wv, LampUp ) ) / ( dv * TanAlpha );
				cp = 1.0;
			}
			if( hooked( kHookNoWindow ) )
				cp = 1.0;
			float cg  = clamp( ( rampEdge( 1.0, wg ) - length( g ) ) / ( 2.0 * wg ) + 0.5, 0.0, 1.0 );
			float cov = cp * cg;
			if( cov <= 0.0 )
				continue;
			covSum += cov;

			float omega = FacetSize * FacetSize * ( hooked( kHookNoFacetCos ) ? 1.0 : un ) / ( hooked( kHookNoInvSquare ) ? 1.0 : D * D );
			float cosP  = max( dot( NP, -u ), 0.0 );
			vec2 gg     = hooked( kHookUnmirrored ) ? vec2( -g.x, g.y ) : g;
			vec3 gob    = goboAt( gg, 2.0 * wg );
			lastGobo    = gob;
			lastCov     = cov;
			sum += Luminance * gob * ( cov * omega * cosP );
		}
	}
	return sum;
}

//---------------------------------------------------------------------------
// The spill: the pin spot's own light past the ball, with the ball's shadow,
// softened by the pupil's size.
//---------------------------------------------------------------------------
vec3 spill( vec3 P, vec3 NP, float foot, out vec3 gob, out float cov )
{
	gob        = vec3( 0.0 );
	cov        = 0.0;
	vec3 d     = P - LampPos;
	float dist = length( d );
	vec3 w     = d / dist;
	float den  = dot( w, LampAxis );
	if( den <= 1e-4 )
		return vec3( 0.0 );
	vec2 g   = vec2( dot( w, LampRight ), dot( w, LampUp ) ) / ( den * TanAlpha );
	float wg = ( PupilRadius + 0.5 * foot ) / ( dist * TanAlpha );
	float cg = clamp( ( rampEdge( 1.0, wg ) - length( g ) ) / ( 2.0 * wg ) + 0.5, 0.0, 1.0 );
	vec3 toC    = -LampPos;
	float along = dot( toC, w );
	if( along > 0.0 && along < dist )
	{
		float perp = length( toC - along * w );
		float hw   = PupilRadius * ( dist - along ) / dist + 0.5 * foot;
		cg *= clamp( ( perp - rampEdge( BallRadius, hw ) ) / ( 2.0 * hw ) + 0.5, 0.0, 1.0 );
	}
	if( cg <= 0.0 )
		return vec3( 0.0 );
	gob        = goboAt( g, 2.0 * wg );
	cov        = cg;
	float cosP = max( dot( NP, -w ), 0.0 );
	return Intensity * gob * ( cg * cosP / ( dist * dist + PupilRadius * PupilRadius ) );
}

//---------------------------------------------------------------------------
// The ball, seen: a facet mirrors the room, and glints where it shows the
// pupil; grout and missing facets are a dark core.
//---------------------------------------------------------------------------
bool ball( vec3 o, vec3 w, out vec3 display )
{
	display = vec3( 0.0 );
	float R = BallRadius;
	float b = dot( o, w );
	float c = dot( o, o ) - R * R;
	float h = b * b - c;
	if( h <= 0.0 )
		return false;
	float t = -b - sqrt( h );
	if( t <= 0.0 )
		return false;
	vec3 X  = o + t * w;
	vec3 nh = X / R;
	display = vec3( 0.02 * RoomLight );
	float lat = atan2Rad( nh.y, length( nh.xz ) );
	int k     = int( floor( lat / Delta + 0.5 * float( Rings - 1 ) + 0.5 ) );
	if( k < 0 || k >= Rings )
		return true;
	vec4 ring = texelFetch( RingTable, ivec2( k, 0 ), 0 );
	int Nk    = int( ring.z + 0.5 );
	float thb = atan2Turn( -nh.z, nh.x ) - Psi;
	int j     = wrapIndex( int( floor( thb * float( Nk ) - ring.w + 0.5 ) ), Nk );
	if( MissingBelow > 0u && facetHash( k, j, Seed ) < MissingBelow )
		return true;
	float th = ( float( j ) + ring.w ) / float( Nk ) + Psi;
	float ct = cosTurn( th ), st = sinTurn( th );
	vec3 n   = vec3( ring.y * ct, ring.x, -ring.y * st );
	vec3 e   = vec3( -st, 0.0, -ct );
	vec3 nn  = vec3( -ring.x * ct, ring.y, ring.x * st );
	vec3 loc = X - R * n;
	if( abs( dot( loc, e ) ) > 0.5 * FacetSize || abs( dot( loc, nn ) ) > 0.5 * FacetSize )
		return true;
	vec3 rv = w - 2.0 * dot( w, n ) * n;
	display = vec3( Reflectance * RoomLight * WallAlbedo );
	float dn = dot( rv, LampAxis );
	if( dn < 0.0 )
	{
		float tq = dot( LampPos - X, LampAxis ) / dn;
		if( tq > 0.0 && length( X + tq * rv - LampPos ) < PupilRadius )
		{
			vec3 wi  = -rv;
			float d2 = dot( wi, LampAxis );
			vec2 g   = vec2( dot( wi, LampRight ), dot( wi, LampUp ) ) / ( d2 * TanAlpha );
			if( length( g ) <= 1.0 )
				display += kPi * Luminance * goboAt( g, 0.0 ) * Gain;
		}
	}
	return true;
}

void main()
{
	vec2 pix  = gl_FragCoord.xy - vec2( Viewport.xy );
	vec4 clip = vec4( 0.0 );
	if( IsOver != 0 )
	{
		clip = texelFetch( InputTexture, ivec2( pix ), 0 );
		if( MixAmount <= 0.0 && ( Hooks & ( kHookRaw | kHookGoboOut ) ) == 0 )
		{
			fragColour = clip;
			return;
		}
	}
	vec2 ndc     = pix / vec2( Viewport.zw ) * 2.0 - 1.0;
	vec3 P       = vec3( 0.0 ), N = vec3( 0.0, 0.0, 1.0 );
	float albedo = WallAlbedo;
	float foot   = 0.0;
	if( ViewMode == 1 )
	{
		P    = PlaneCentre + ndc.x * PlaneHalf.x * PlaneRight + ndc.y * PlaneHalf.y * PlaneUp;
		N    = PlaneNormal;
		foot = 2.0 * PlaneHalf.y / float( Viewport.w );
	}
	else
	{
		vec3 w = normalize( Forward + ndc.x * TanHalf.x * Right + ndc.y * TanHalf.y * Up );
		vec3 seen;
		if( ball( Eye, w, seen ) )
		{
			bool measuring = ( Hooks & ( kHookRaw | kHookGoboOut | kHookSpotsOnly ) ) != 0;
			fragColour     = measuring ? vec4( 0.0 ) : vec4( toCode( min( seen, vec3( 1.0 ) ) ), 1.0 );
			return;
		}
		float tExit = 1e30;
		int axis    = 0;
		for( int i = 0; i < 3; ++i )
		{
			float t = 1e30;
			if( w[ i ] > 1e-9 )
				t = ( BoxMax[ i ] - Eye[ i ] ) / w[ i ];
			else if( w[ i ] < -1e-9 )
				t = ( BoxMin[ i ] - Eye[ i ] ) / w[ i ];
			if( t < tExit )
			{
				tExit = t;
				axis  = i;
			}
		}
		P         = Eye + tExit * w;
		N         = vec3( 0.0 );
		N[ axis ] = w[ axis ] > 0.0 ? -1.0 : 1.0;
		albedo    = axis == 1 ? ( w.y < 0.0 ? FloorAlbedo : CeilingAlbedo ) : WallAlbedo;
		foot      = tExit * 2.0 * TanHalf.y / float( Viewport.w ) / max( abs( dot( N, w ) ), 0.2 );
	}

	float covSum, visited, gcov;
	vec3 gob;
	vec3 E = gather( P, N, foot, covSum, visited, gob, gcov );
	if( !hooked( kHookSpotsOnly ) )
	{
		vec3 sg;
		float sc;
		E += spill( P, N, foot, sg, sc );
		if( gcov <= 0.0 )
		{
			gob  = sg;
			gcov = sc;
		}
	}
	if( hooked( kHookGoboOut ) )
	{
		fragColour = vec4( gob, gcov );
		return;
	}
	if( hooked( kHookRaw ) )
	{
		fragColour = vec4( hooked( kHookCoverage ) ? vec3( covSum ) : E, hooked( kHookCount ) ? visited : 1.0 );
		return;
	}

	//Exposure: albedo x ( room light + E / E_ref x 2^Exposure ), clamped in
	//linear, no tone curve: Room Light 1 with no spots returns the albedo.
	vec3 light = vec3( RoomLight ) + E * Gain;
	if( IsOver == 0 )
	{
		fragColour = vec4( toCode( min( albedo * light, vec3( 1.0 ) ) ), 1.0 );
		return;
	}
	float m = clamp( MixAmount, 0.0, 1.0 );
	float a = clamp( clip.a, 0.0, 1.0 );
	if( ClipAs == 0 )
	{
		//Surface: the clip is the wall. Its alpha is the wall's: no surface,
		//no light, and nothing under a transparent pixel shows.
		vec3 lit = toCode( min( toLinear( clamp( clip.rgb, 0.0, 1.0 ) ) * light, vec3( 1.0 ) ) );
		vec3 rgb = mix( clip.rgb, lit, m );
		if( a <= 0.0 && !hooked( kHookAlphaUnmasked ) )
			rgb = vec3( 0.0 );
		fragColour = vec4( rgb, a );
		return;
	}
	//Gobo: the room is opaque wherever Mix shows it; the mix is premultiplied
	//so nothing under a transparent pixel shows at any Mix.
	vec3 room  = toCode( min( vec3( WallAlbedo ) * light, vec3( 1.0 ) ) );
	float aOut = hooked( kHookAlphaThrough ) ? a : mix( a, 1.0, m );
	vec3 pre   = clamp( clip.rgb, 0.0, 1.0 ) * a * ( 1.0 - m ) + room * m;
	fragColour = vec4( aOut > 0.0 ? pre / aOut : vec3( 0.0 ), aOut );
}
)";

std::string Assemble( Pass pass )
{
	const std::string text = std::string( kVersion ) + kCommon;
	switch( pass )
	{
	case Pass::Gobo: return text + kGoboFragment;
	case Pass::Final: return text + kFinalFragment;
	}
	return text;
}

} // namespace mirrorball::shaders
