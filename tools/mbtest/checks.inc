//===========================================================================
// The checks. Each sets every control it depends on (honeydew's lesson), and
// every GL check runs at each raster in kRasters. Pictures are read as linear
// irradiance (lux) or coverage through the test hooks, with the spill and
// the room light off unless a check is about them.
//===========================================================================

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.SetHooksForTest( perturb.hooks );
	rig.plugin.SetMotorHooksForTest( perturb.motorHooks );
	rig.plugin.SetPluginHooksForTest( perturb.pluginHooks );
	return true;
}

//---------------------------------------------------------------------------
// Geometry the checks share. Nothing here is the shader's physics: it is
// where the facets are (the layout, Ball.cpp's definition), where a plane's
// pixels are, and closed forms built from those.
//---------------------------------------------------------------------------
//SPEC-mirrorball.md's numbers, written again here rather than read from
//the code, so a slip in the code's copy fails a check instead of being
//shared by the plugin and its reference.
constexpr double kA               = 0.114;//the PAR36's diameter, the exit pupil (GE 4515)
constexpr double kSpecReflectance = 0.86; //the facet's glass mirror
constexpr double kSpecLux = 4120.0, kSpecMetres = 2.0, kSpecBeam = 6.0;//Chauvet LED Pinspot 2

double deg( double radians )
{
	return radians * 180.0 / kPi;
}
double rad( double degrees )
{
	return degrees * kPi / 180.0;
}

/// Where a linear ramp of half-width w must sit on the edge of a disc of
/// radius r for the ramped disc to hold the disc's area (the shader's
/// rampEdge, written again from SPEC-mirrorball.md's As built): it is also
/// where a disc blurred by a square of half-width w crosses one half, to
/// second order.
double rampEdgeRef( double r, double w )
{
	if( 3.0 * r * r >= 4.0 * w * w )
		return std::sqrt( r * r - w * w / 3.0 );
	return std::cbrt( 6.0 * w * r * r ) - w;
}

/// The lamp as the spec has it: the frame (axis, right, up) from the code's
/// convention, the beam's half-angle as the control was set, and the
/// photometry from the Chauvet anchor: the beam's flux held as the lens
/// changes, I0 = Phi / ( 2 pi ( 1 - cos alpha ) ), L_e = I0 / pi ( A / 2 )^2.
lamp::Lamp specLamp( const Vec3& position, double beamDegrees )
{
	lamp::Lamp l      = lamp::At( position, beamDegrees );
	l.alpha           = 0.5 * beamDegrees * kPi / 180.0;
	l.tanAlpha        = std::tan( l.alpha );
	const double flux = kSpecLux * kSpecMetres * kSpecMetres * 2.0 * kPi * ( 1.0 - std::cos( 0.5 * kSpecBeam * kPi / 180.0 ) );
	l.intensity       = flux / ( 2.0 * kPi * ( 1.0 - std::cos( l.alpha ) ) );
	l.luminance       = l.intensity / ( kPi * 0.25 * kA * kA );
	return l;
}

/// The layout the plugin builds for these controls (the same float round
/// trip the host's parameter takes).
ball::Layout layoutFor( double diameter, double facet )
{
	return ball::Build( DiameterFromParam( ParamFromDiameter( diameter ) ), FacetFromParam( ParamFromFacet( facet ) ) );
}

void setBall( Rig& rig, double diameter, double facet )
{
	rig.Set( PT_DIAMETER, ParamFromDiameter( diameter ) );
	rig.Set( PT_FACET_SIZE, ParamFromFacet( facet ) );
	rig.Set( PT_MISSING, 0.0f );
}

/// The facet whose normal (at psi = 0) is nearest `target`.
void facetNearest( const ball::Layout& layout, const Vec3& target, int& ring, int& index )
{
	double best = -2.0;
	ring = index = 0;
	for( int k = 0; k < layout.Rings(); ++k )
		for( int j = 0; j < layout.rings[ static_cast< size_t >( k ) ].count; ++j )
		{
			const double c = dot( ball::NormalOf( layout.rings[ static_cast< size_t >( k ) ], j, 0.0 ), target );
			if( c > best )
			{
				best  = c;
				ring  = k;
				index = j;
			}
		}
}

/// A unit vector at `angle` from `v`, turned about an axis perpendicular to it.
Vec3 tilted( const Vec3& v, double angle, const Vec3& hint = { 0.0, 1.0, 0.0 } )
{
	Vec3 axis = cross( v, hint );
	if( length( axis ) < 1e-6 )
		axis = cross( v, Vec3 { 1.0, 0.0, 0.0 } );
	axis           = normalise( axis );
	const Vec3 out = cross( axis, v );
	return normalise( std::cos( angle ) * v + std::sin( angle ) * out );
}

Vec3 upHintFor( const Vec3& normal )
{
	return std::fabs( normal[ 1 ] ) > 0.9 ? Vec3 { 0.0, 0.0, -1.0 } : Vec3 { 0.0, 1.0, 0.0 };
}

/// An orthographic test plane as the plugin draws it, at one raster.
struct PlaneFrame
{
	room::View view;
	double halfW = 1.0, halfH = 1.0;
	int w = 1, h = 1;

	Vec3 worldOf( double px, double py ) const///< pixel coordinates, centres at i + 0.5
	{
		const double nx = px / w * 2.0 - 1.0, ny = py / h * 2.0 - 1.0;
		return view.centre + ( nx * halfW ) * view.right + ( ny * halfH ) * view.up;
	}
	double pixel() const
	{
		return 2.0 * halfH / h;
	}
	/// A world point's coordinates on the plane, metres from its centre.
	void planeOf( const Vec3& p, double& x, double& y ) const
	{
		x = dot( p - view.centre, view.right );
		y = dot( p - view.centre, view.up );
	}
};

PlaneFrame frameOf( const TestRig& t, int w, int h )
{
	PlaneFrame f;
	f.view  = room::Plane( t.centre, t.normal, t.upHint, t.halfHeight );
	f.w     = w;
	f.h     = h;
	f.halfH = t.halfHeight;
	f.halfW = t.halfHeight * w / h;
	return f;
}

/// Where a ray meets a plane (centre c, normal nn); false when it does not.
bool hitPlane( const Vec3& origin, const Vec3& dir, const Vec3& c, const Vec3& nn, Vec3& at )
{
	const double den = dot( dir, nn );
	if( std::fabs( den ) < 1e-12 )
		return false;
	const double t = dot( c - origin, nn ) / den;
	if( t <= 0.0 )
		return false;
	at = origin + t * dir;
	return true;
}

//---------------------------------------------------------------------------
// Measurements out of the picture.
//---------------------------------------------------------------------------
struct Blob
{
	double sum = 0.0, cx = 0.0, cy = 0.0;
	double sxx = 0.0, syy = 0.0, sxy = 0.0;///< central second moments (pixels^2)
	double peak = 0.0;
	long count  = 0;
	bool edge   = false;///< anything lit within two pixels of the frame's edge
};

/// Value-weighted: the sum, the centroid, the moments, of one channel.
Blob weighted( const Floats& px, int w, int h, int channel = 1 )
{
	Blob b;
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
			b.peak = std::max( b.peak, static_cast< double >( px[ ( static_cast< size_t >( y ) * w + x ) * 4 + channel ] ) );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			const double v = px[ ( static_cast< size_t >( y ) * w + x ) * 4 + channel ];
			if( !( v > 0.0 ) )
				continue;
			b.sum += v;
			b.cx += v * ( x + 0.5 );
			b.cy += v * ( y + 0.5 );
			if( ( x < 2 || y < 2 || x >= w - 2 || y >= h - 2 ) && v > 1e-4 * b.peak )
				b.edge = true;
		}
	if( b.sum > 0.0 )
	{
		b.cx /= b.sum;
		b.cy /= b.sum;
	}
	return b;
}

/// Thresholded: the pixels at or above `level`, their count, centroid and
/// second moments.
Blob region( const Floats& px, int w, int h, double level, int channel = 1 )
{
	Blob b;
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			const double v = px[ ( static_cast< size_t >( y ) * w + x ) * 4 + channel ];
			b.peak         = std::max( b.peak, v );
			if( v > 1e-6 && ( x < 2 || y < 2 || x >= w - 2 || y >= h - 2 ) )
				b.edge = true;
			if( v >= level )
			{
				++b.count;
				b.cx += x + 0.5;
				b.cy += y + 0.5;
			}
		}
	if( b.count == 0 )
		return b;
	b.cx /= b.count;
	b.cy /= b.count;
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
			if( px[ ( static_cast< size_t >( y ) * w + x ) * 4 + channel ] >= level )
			{
				const double dx = x + 0.5 - b.cx, dy = y + 0.5 - b.cy;
				b.sxx += dx * dx;
				b.syy += dy * dy;
				b.sxy += dx * dy;
			}
	b.sxx /= b.count;
	b.syy /= b.count;
	b.sxy /= b.count;
	return b;
}

/// The moments' principal axes: sqrt of the eigenvalue ratio.
double axisRatio( const Blob& b )
{
	const double tr = b.sxx + b.syy, det = b.sxx * b.syy - b.sxy * b.sxy;
	const double disc = std::sqrt( std::max( 0.0, 0.25 * tr * tr - det ) );
	const double l1 = 0.5 * tr + disc, l2 = 0.5 * tr - disc;
	return l2 > 0.0 ? std::sqrt( l1 / l2 ) : 0.0;
}

/// A NaN-propagating max for worst cases: std::max drops a NaN on one side.
double worse( double a, double b )
{
	if( std::isnan( a ) || std::isnan( b ) )
		return std::numeric_limits< double >::quiet_NaN();
	return std::max( a, b );
}

/// An optics rig: one facet, linear light out, no spill, no room light, the
/// motor bypassed at psi 0, the lamp exactly where the check puts it.
TestRig opticsRig( const Vec3& lampPos, int ring, int index, int kMax )
{
	TestRig t;
	t.lamp      = true;
	t.lampPos   = lampPos;
	t.psi       = true;
	t.psiTurns  = 0.0;
	t.soloRing  = ring;
	t.soloIndex = index;
	t.kMax      = kMax;
	return t;
}

void opticsControls( Rig& rig, double beamDegrees, double diameter, double facet )
{
	setBall( rig, diameter, facet );
	rig.Set( PT_BEAM, ParamFromBeam( beamDegrees ) );
	rig.Set( PT_GOBO, static_cast< float >( Gobo::Open ) );
	rig.Set( PT_COLOUR, static_cast< float >( Colour::White ) );
	rig.Set( PT_ROOM_LIGHT, 0.0f );
	rig.Set( PT_EXPOSURE, ParamFromExposure( 0.0 ) );
	rig.Set( PT_MOTOR, static_cast< float >( Motor::Free ) );
	rig.Set( PT_SPEED, ParamFromSpeed( 0.0 ) );
}

/// One frame of one facet on one plane.
Floats shootPlane( Rig& rig, TestRig t, const Vec3& centre, const Vec3& normal, double halfHeight, int hooks )
{
	t.plane      = true;
	t.centre     = centre;
	t.normal     = normal;
	t.upHint     = upHintFor( normal );
	t.halfHeight = halfHeight;
	rig.plugin.SetRigForTest( t );
	rig.plugin.SetHooksForTest( hooks );
	if( !rig.Render( 1 ) )
		return {};
	return rig.Output();
}

/// The bound on a centroid read off a picture whose edge is a linear ramp
/// `rampPx` pixels wide (half-width) on either side: the midpoint rule errs
/// by at most |slope change| h^2 / 8 per unit of edge, which over a blob
/// moves the centroid by at most 1 / ( 4 rampPx ) of a pixel.
double centroidBound( double rampPx )
{
	return 1.0 / ( 4.0 * std::max( rampPx, 0.5 ) );
}

//===========================================================================
// --reflect: the law of reflection, measured out of the picture.
//===========================================================================
int runReflect( const Perturb& perturb )
{
	std::printf( "\n=== reflect: a facet's spot on two parallel planes gives its reflected direction; the angles to the normal agree and the rays share a plane\n" );
	const Vec3 lamps[]    = { { 4.0, 1.0, 3.0 }, { -3.0, -2.0, 4.0 }, { 0.5, 4.5, -2.5 } };
	const double gammas[] = { 15.0, 35.0, 55.0 };
	const double kDist[2] = { 1.5, 3.0 };
	const double beam     = 12.0, diameter = 0.5, facet = 0.01;
	const ball::Layout layout = layoutFor( diameter, facet );
	for( const Raster& raster : kRasters )
	{
		double worstAngle = 0.0, worstPlane = 0.0, worstApex = 0.0, worstRatio = 0.0;
		int measured = 0, cases = 0;
		for( const Vec3& lampPos : lamps )
			for( double gamma : gammas )
			{
				++cases;
				int k = 0, j = 0;
				facetNearest( layout, tilted( normalise( lampPos ), rad( gamma ) ), k, j );
				const Vec3 n   = ball::NormalOf( layout.rings[ static_cast< size_t >( k ) ], j, 0.0 );
				const Vec3 X   = layout.radius * n;
				const Vec3 win = normalise( X - lampPos );
				const Vec3 r   = reflect( win, n );
				const double L = length( X - lampPos );
				Rig rig;
				if( !startRig( rig, raster.w, raster.h, perturb ) )
					return 1;
				opticsControls( rig, beam, diameter, facet );
				const TestRig t = opticsRig( lampPos, k, j, perturb.kMax );
				Vec3 c[ 2 ];
				double bound[ 2 ] = {}, beta = 0.0;
				bool ok           = true;
				for( int p = 0; p < 2; ++p )
				{
					const double D      = kDist[ p ];
					const double radius = 0.5 * kA * D / L + 0.5 * layout.facet * ( 1.0 + D / L );
					const double half   = 3.0 * radius;
					const Vec3 centre   = X + D * r;
					const Floats px     = shootPlane( rig, t, centre, -1.0 * r, half, perturb.hooks | shaders::kHookRaw | shaders::kHookSpotsOnly );
					TestRig tt          = t;
					tt.plane            = true;
					tt.centre           = centre;
					tt.normal           = -1.0 * r;
					tt.upHint           = upHintFor( tt.normal );
					tt.halfHeight       = half;
					const PlaneFrame f  = frameOf( tt, raster.w, raster.h );
					const Blob b        = weighted( px, raster.w, raster.h );
					if( px.empty() || !( b.sum > 0.0 ) || b.edge )
					{
						ok = false;
						break;
					}
					c[ p ]          = f.worldOf( b.cx, b.cy );
					const double wp = 0.5 * layout.facet * ( D + L ) / L;//the pupil ramp's half-width on the plane, m
					bound[ p ]      = centroidBound( wp / f.pixel() ) * f.pixel();
					beta            = std::max( beta, radius / D );
				}
				if( !ok )
				{
					worstAngle = worse( worstAngle, std::numeric_limits< double >::quiet_NaN() );
					continue;
				}
				++measured;
				const Vec3 rm      = normalise( c[ 1 ] - c[ 0 ] );
				const double sep   = length( c[ 1 ] - c[ 0 ] );
				const double tanF  = std::tan( std::acos( std::min( 1.0, dot( -1.0 * win, n ) ) ) );
				const double tol   = ( bound[ 0 ] + bound[ 1 ] ) / sep + beta * beta * ( 1.0 + tanF );
				const double dAng  = std::fabs( std::acos( std::clamp( dot( rm, n ), -1.0, 1.0 ) ) - std::acos( std::clamp( dot( -1.0 * win, n ), -1.0, 1.0 ) ) );
				const double plane = std::fabs( dot( cross( win, n ), rm ) );
				const double apex  = length( cross( X - c[ 0 ], rm ) );
				worstAngle         = worse( worstAngle, dAng );
				worstPlane         = worse( worstPlane, plane );
				worstApex          = worse( worstApex, apex );
				worstRatio         = worse( worstRatio, std::max( dAng, plane ) / tol );
				worstRatio         = worse( worstRatio, apex / ( bound[ 0 ] + kDist[ 0 ] * tol ) );
			}
		Check( measured == cases && worstRatio <= 1.0,
		       fmt( "%dx%d, %d facets (incidence 15/35/55 deg, three lamps), %d measured: angle of reflection off the angle of incidence by at most %.2e rad, "
		            "out of the plane of incidence by %.2e, the line misses the facet by %.2e m; worst at %.2f of its bound "
		            "(two centroids' midpoint-rule bounds over the planes' separation, plus beta^2 (1 + tan theta_f) for the facet's cosine across the spot)",
		            raster.w, raster.h, cases, measured, worstAngle, worstPlane, worstApex, worstRatio ) );
	}
	return Verdict();
}

//===========================================================================
// --area: the spot's 0.5 contour against the cone cut by a plane.
//===========================================================================
/// The ellipse a cone (half-angle alpha) cuts from a plane at perpendicular
/// distance h, its axis at incidence i: SPEC-mirrorball.md's closed form.
void ellipseOf( double h, double i, double alpha, double& area, double& a, double& b )
{
	const double c2 = std::cos( i ) * std::cos( i ), s2 = std::sin( alpha ) * std::sin( alpha );
	area = kPi * h * h * s2 * std::cos( alpha ) / std::pow( c2 - s2, 1.5 );
	a    = h * std::sin( alpha ) * std::cos( alpha ) / ( c2 - s2 );
	b    = h * std::sin( alpha ) / std::sqrt( c2 - s2 );
}

/// The far field's 0.5 contour, constructed: rays from the pupil's rim
/// through the facet's centre, reflected, met with the plane. Its area, its
/// bounding box (plane coordinates) for framing.
struct Outline
{
	double area = 0.0, xmin = 1e30, xmax = -1e30, ymin = 1e30, ymax = -1e30, perimeter = 0.0;
	bool ok = true;
};

Outline outlineOf( const std::vector< Vec3 >& rays, const Vec3& X, const room::View& plane )
{
	Outline o;
	std::vector< std::array< double, 2 > > pts;
	for( const Vec3& d : rays )
	{
		Vec3 at;
		if( !hitPlane( X, d, plane.centre, plane.normal, at ) )
		{
			o.ok = false;
			return o;
		}
		const double x = dot( at - plane.centre, plane.right ), y = dot( at - plane.centre, plane.up );
		pts.push_back( { x, y } );
		o.xmin = std::min( o.xmin, x );
		o.xmax = std::max( o.xmax, x );
		o.ymin = std::min( o.ymin, y );
		o.ymax = std::max( o.ymax, y );
	}
	for( size_t i = 0; i < pts.size(); ++i )
	{
		const auto& p = pts[ i ];
		const auto& q = pts[ ( i + 1 ) % pts.size() ];
		o.area += p[ 0 ] * q[ 1 ] - q[ 0 ] * p[ 1 ];
		o.perimeter += std::hypot( q[ 0 ] - p[ 0 ], q[ 1 ] - p[ 1 ] );
	}
	o.area = 0.5 * std::fabs( o.area );
	return o;
}

std::vector< Vec3 > gateCone( const Vec3& axis, double alpha, int n = 2000 )
{
	Vec3 e1 = normalise( cross( axis, upHintFor( axis ) ) ), e2 = cross( axis, e1 );
	std::vector< Vec3 > rays;
	for( int i = 0; i < n; ++i )
	{
		const double t = 2.0 * kPi * i / n;
		rays.push_back( normalise( std::cos( alpha ) * axis + std::sin( alpha ) * ( std::cos( t ) * e1 + std::sin( t ) * e2 ) ) );
	}
	return rays;
}

std::vector< Vec3 > pupilRim( const lamp::Lamp& l, const Vec3& X, const Vec3& n, int count = 2000 )
{
	std::vector< Vec3 > rays;
	for( int i = 0; i < count; ++i )
	{
		const double t  = 2.0 * kPi * i / count;
		const Vec3 q    = l.position + ( 0.5 * kA ) * ( std::cos( t ) * l.right + std::sin( t ) * l.up );
		rays.push_back( reflect( normalise( X - q ), n ) );
	}
	return rays;
}

/// A plane at incidence `i` to `axis`, through the point `distance` along it
/// from X, framed (centred and sized) on the outline the rays draw on it.
TestRig framePlane( TestRig t, const Vec3& X, const Vec3& axis, double distance, double i, const std::vector< Vec3 >& rays, double margin, int w,
                    int h, Outline& outline )
{
	const Vec3 normal = -1.0 * tilted( axis, i );
	t.plane           = true;
	t.normal          = normal;
	t.upHint          = upHintFor( normal );
	t.centre          = X + distance * axis;
	t.halfHeight      = 1.0;
	room::View v      = room::Plane( t.centre, t.normal, t.upHint, 1.0 );
	Outline o         = outlineOf( rays, X, v );
	t.centre          = t.centre + ( 0.5 * ( o.xmin + o.xmax ) ) * v.right + ( 0.5 * ( o.ymin + o.ymax ) ) * v.up;
	t.halfHeight      = margin * std::max( 0.5 * ( o.ymax - o.ymin ), 0.5 * ( o.xmax - o.xmin ) * h / w );
	v                 = room::Plane( t.centre, t.normal, t.upHint, t.halfHeight );
	outline           = outlineOf( rays, X, v );
	return t;
}

int runArea( const Perturb& perturb )
{
	std::printf( "\n=== area: near the lamp a spot is the gate's cone cut by the wall (the oblique ellipse); far from it, the pupil seen through the facet\n" );
	const double diameter = 0.5, facet = 0.01, beam = 5.0;
	const ball::Layout layout = layoutFor( diameter, facet );
	const Vec3 toLamp         = normalise( Vec3 { 0.3, 0.4, 1.0 } );
	for( const Raster& raster : kRasters )
	{
		//Near field: the lamp 0.7 m from the centre, L* = 1.31 m at 5 deg.
		{
			const Vec3 lampPos = 0.7 * toLamp;
			int k = 0, j = 0;
			facetNearest( layout, toLamp, k, j );
			const Vec3 n           = ball::NormalOf( layout.rings[ static_cast< size_t >( k ) ], j, 0.0 );
			const Vec3 X           = layout.radius * n;
			const lamp::Lamp l     = specLamp( lampPos, beam );
			const Vec3 axis        = reflect( l.axis, n );
			double worstRel = 0.0, worstRatio = 0.0, worstOverBound = 0.0;
			int measured = 0;
			std::string detail;
			for( double inc : { 0.0, 30.0, 50.0, 60.0 } )
			{
				const double hPerp = 1.5;
				Outline o;
				const std::vector< Vec3 > rays = gateCone( axis, l.alpha );
				TestRig t = framePlane( opticsRig( lampPos, k, j, perturb.kMax ), X, axis, hPerp / std::cos( rad( inc ) ), rad( inc ), rays, 1.25, raster.w, raster.h, o );
				Rig rig;
				if( !startRig( rig, raster.w, raster.h, perturb ) )
					return 1;
				opticsControls( rig, beam, diameter, facet );
				const Floats px = shootPlane( rig, t, t.centre, t.normal, t.halfHeight, perturb.hooks | shaders::kHookRaw | shaders::kHookCoverage | shaders::kHookSpotsOnly );
				const PlaneFrame f = frameOf( t, raster.w, raster.h );
				const Blob b       = region( px, raster.w, raster.h, 0.5 );
				double area = 0.0, a = 0.0, bb = 0.0;
				ellipseOf( hPerp, rad( inc ), l.alpha, area, a, bb );
				const double pix   = f.pixel();
				//The 0.5 contour sits at rampEdge in gobo units (the ramp's
				//width at the axis's distance).
				const double wg    = 0.5 * ( layout.facet + pix ) / ( hPerp / std::cos( rad( inc ) ) * l.tanAlpha );
				const double shrink = rampEdgeRef( 1.0, wg );
				area *= shrink * shrink;
				const double got   = b.count * pix * pix;
				const double perim = kPi * ( 3.0 * ( a + bb ) - std::sqrt( ( 3.0 * a + bb ) * ( a + 3.0 * bb ) ) ) / pix;//Ramanujan, pixels
				const double bound = ( std::sqrt( 2.0 ) * perim + 4.0 ) / ( area / ( pix * pix ) );
				const double rel   = got / area - 1.0;
				const double ratio = axisRatio( b ) / ( a / bb ) - 1.0;
				if( b.count > 0 && !b.edge )
					++measured;
				worstRel       = worse( worstRel, std::fabs( rel ) );
				worstRatio     = worse( worstRatio, std::fabs( ratio ) );
				worstOverBound = worse( worstOverBound, std::max( std::fabs( rel ) / bound, std::fabs( ratio ) / ( 4.0 * bound ) ) );
				detail += fmt( " %g deg: %.4f m^2 (closed form %.4f), a/b %.3f (%.3f);", inc, got, area, axisRatio( b ), a / bb );
			}
			Check( measured == 4 && worstOverBound <= 1.0,
			       fmt( "%dx%d near field (lamp 0.70 m, L* 1.31 m):%s worst area %.2e, axis ratio %.2e; at %.2f of the boundary-pixel bound", raster.w, raster.h,
			            detail.c_str(), worstRel, worstRatio, worstOverBound ) );
		}
		//Far field: the lamp 5 m away; the contour is the pupil's rim
		//projected through the facet's centre.
		{
			const Vec3 lampPos = 5.0 * toLamp;
			int k = 0, j = 0;
			facetNearest( layout, toLamp, k, j );
			const Vec3 n       = ball::NormalOf( layout.rings[ static_cast< size_t >( k ) ], j, 0.0 );
			const Vec3 X       = layout.radius * n;
			const lamp::Lamp l = specLamp( lampPos, beam );
			const Vec3 axis    = reflect( normalise( X - lampPos ), n );
			const std::vector< Vec3 > rays = pupilRim( l, X, n );
			double worstRel = 0.0, worstOverBound = 0.0;
			int measured = 0;
			std::string detail;
			for( const auto& place : { std::array< double, 2 > { 2.0, 0.0 }, std::array< double, 2 > { 3.0, 45.0 } } )
			{
				Outline o;
				TestRig t = framePlane( opticsRig( lampPos, k, j, perturb.kMax ), X, axis, place[ 0 ], rad( place[ 1 ] ), rays, 1.4, raster.w, raster.h, o );
				Rig rig;
				if( !startRig( rig, raster.w, raster.h, perturb ) )
					return 1;
				opticsControls( rig, beam, diameter, facet );
				const Floats px = shootPlane( rig, t, t.centre, t.normal, t.halfHeight, perturb.hooks | shaders::kHookRaw | shaders::kHookCoverage | shaders::kHookSpotsOnly );
				const PlaneFrame f = frameOf( t, raster.w, raster.h );
				const Blob b       = region( px, raster.w, raster.h, 0.5 );
				const double pix   = f.pixel();
				const double got   = b.count * pix * pix;
				const double bound = ( std::sqrt( 2.0 ) * o.perimeter / pix + 4.0 ) / ( o.area / ( pix * pix ) );
				//The 0.5 contour sits at rampEdge on the pupil's plane.
				const double Lx     = dot( X - lampPos, l.axis );
				const double wp     = 0.5 * layout.facet * ( place[ 0 ] + Lx ) / place[ 0 ] + 0.5 * pix * Lx / place[ 0 ];
				const double shrink = rampEdgeRef( 0.5 * kA, wp ) / ( 0.5 * kA );
				const double rel    = got / ( o.area * shrink * shrink ) - 1.0;
				if( b.count > 0 && !b.edge && o.ok )
					++measured;
				worstRel       = worse( worstRel, std::fabs( rel ) );
				worstOverBound = worse( worstOverBound, std::fabs( rel ) / bound );
				detail += fmt( " %g m at %g deg: %.3e m^2 (constructed %.3e);", place[ 0 ], place[ 1 ], got, o.area );
			}
			Check( measured == 2 && worstOverBound <= 1.0,
			       fmt( "%dx%d far field (lamp 5 m):%s worst %.2e, at %.2f of the boundary-pixel bound", raster.w, raster.h, detail.c_str(), worstRel, worstOverBound ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --flux: what one facet sends, summed off the plane, against what it
// intercepts.
//===========================================================================
/// rho L_e s^2 times the integral of max( 0, -w.n ) over the directions
/// that reach the facet through the pupil and the gate: by quadrature over
/// the pupil (far field; assumes every ray inside the gate) or over the
/// gate's cone (near field; assumes every ray inside the pupil). `extent` is
/// how close the assumption came to failing: the largest |g| a pupil ray
/// had (far), or the largest distance from the pupil's centre a gate ray
/// crossed its plane at (near, metres).
double fluxIntercepted( const lamp::Lamp& l, const Vec3& X, const Vec3& n, double s, bool near, double& extent )
{
	extent          = 0.0;
	const int nr    = 400, nt = 400;
	double integral = 0.0;
	if( !near )
	{
		const double a = 0.5 * kA;
		for( int ir = 0; ir < nr; ++ir )
			for( int it = 0; it < nt; ++it )
			{
				const double rr = a * ( ir + 0.5 ) / nr, th = 2.0 * kPi * ( it + 0.5 ) / nt;
				const Vec3 q    = l.position + rr * ( std::cos( th ) * l.right + std::sin( th ) * l.up );
				const Vec3 d    = X - q;
				const double dd = length( d );
				const Vec3 w    = ( 1.0 / dd ) * d;
				const double cq = dot( w, l.axis );
				extent          = std::max( extent, std::hypot( dot( w, l.right ), dot( w, l.up ) ) / ( cq * l.tanAlpha ) );
				const double dA = rr * ( a / nr ) * ( 2.0 * kPi / nt );
				integral += std::max( 0.0, -dot( w, n ) ) * cq / ( dd * dd ) * dA;
			}
	}
	else
	{
		for( int ir = 0; ir < nr; ++ir )
			for( int it = 0; it < nt; ++it )
			{
				const double th = l.alpha * ( ir + 0.5 ) / nr, ph = 2.0 * kPi * ( it + 0.5 ) / nt;
				const Vec3 w    = normalise( std::cos( th ) * l.axis + std::sin( th ) * ( std::cos( ph ) * l.right + std::sin( ph ) * l.up ) );
				const double t  = dot( X - l.position, l.axis ) / dot( w, l.axis );
				extent          = std::max( extent, length( X - t * w - l.position ) );
				integral += std::max( 0.0, -dot( w, n ) ) * std::sin( th ) * ( l.alpha / nr ) * ( 2.0 * kPi / nt );
			}
	}
	return kSpecReflectance * l.luminance * s * s * integral;
}

int runFlux( const Perturb& perturb )
{
	std::printf( "\n=== flux: one facet's light summed over a plane is what it intercepted from the pupil and the gate, at any distance and slant\n" );
	const double diameter = 0.5;
	const Vec3 toLamp     = normalise( Vec3 { 0.3, 0.4, 1.0 } );
	struct Case
	{
		bool near;
		double lampDistance, beam, gamma, facet;
	};
	//The last: a 20 mm facet 0.6 m from the plane, where its ramp is wider
	//than the pupil and never reaches 1 (rampEdge's second branch).
	const Case cases[] = { { false, 5.0, 8.0, 5.0, 0.01 }, { false, 5.0, 8.0, 40.0, 0.01 }, { true, 0.7, 5.0, 0.0, 0.01 }, { false, 5.0, 8.0, 5.0, 0.02 } };
	for( const Raster& raster : kRasters )
	{
		for( const Case& c : cases )
		{
			const ball::Layout layout = layoutFor( diameter, c.facet );
			const Vec3 lampPos        = c.lampDistance * toLamp;
			int k = 0, j = 0;
			facetNearest( layout, tilted( toLamp, rad( c.gamma ) ), k, j );
			const Vec3 n       = ball::NormalOf( layout.rings[ static_cast< size_t >( k ) ], j, 0.0 );
			const Vec3 X       = layout.radius * n;
			const lamp::Lamp l = specLamp( lampPos, c.beam );
			const Vec3 axis    = c.near ? reflect( l.axis, n ) : reflect( normalise( X - lampPos ), n );
			const double tStar = dot( X - lampPos, l.axis );
			double extent      = 0.0;
			const double ref   = fluxIntercepted( l, X, n, layout.facet, c.near, extent );
			const std::vector< Vec3 > rays = c.near ? gateCone( axis, l.alpha ) : pupilRim( l, X, n );
			double worstOverBound = 0.0;
			int measured = 0, places = 0;
			bool assumed = true;
			std::string detail;
			const std::vector< std::array< double, 2 > > planes = { { c.near ? 1.0 : 0.6, 0.0 }, { 2.0, 0.0 }, { 4.0, 0.0 }, { 3.0, 40.0 } };
			for( const auto& place : planes )
			{
				++places;
				const double D = place[ 0 ];
				//The ramps at this distance (the pixel added once framed): the
				//pupil's on its plane (m), the gate's in gobo units.
				const double wp0 = 0.5 * layout.facet * ( D + tStar ) / D, wg0 = 0.5 * layout.facet / ( D * l.tanAlpha );
				const double grow = c.near ? 1.0 + 2.0 * wg0 : 1.0 + 2.0 * wp0 / ( 0.5 * kA );
				Outline o;
				TestRig t = framePlane( opticsRig( lampPos, k, j, perturb.kMax ), X, axis, D, rad( place[ 1 ] ), rays, 1.3 * grow, raster.w, raster.h, o );
				Rig rig;
				if( !startRig( rig, raster.w, raster.h, perturb ) )
					return 1;
				opticsControls( rig, c.beam, diameter, c.facet );
				const Floats px    = shootPlane( rig, t, t.centre, t.normal, t.halfHeight, perturb.hooks | shaders::kHookRaw | shaders::kHookSpotsOnly );
				const PlaneFrame f = frameOf( t, raster.w, raster.h );
				const Blob b       = weighted( px, raster.w, raster.h );
				const double pix   = f.pixel();
				const double got   = b.sum * pix * pix;
				const double wp    = wp0 + 0.5 * pix * tStar / D, wg = 0.5 * ( layout.facet + pix ) / ( D * l.tanAlpha );
				//The quadrature ignored one edge: its ramp must not reach the
				//spot either.
				if( c.near ? extent + 2.0 * wp > 0.5 * kA : extent + 2.0 * wg > 1.0 )
					assumed = false;
				//The bound. The ramp's edge is placed to hold the disc's area
				//(rampEdge), but the light a ray carries is not uniform over the
				//pupil: cos^3 / L^2 and the facet's cosine curve, and a ramp of
				//half-width w carries the disc's second moment up by w^2 / 3,
				//which moves the flux by -(laplacian f / f) w^2 / 6, about
				//-(4 / 3) (w / L)^2 on the pupil's plane (in gobo units the
				//same with L = 1 / tan alpha): 2 (w / L)^2 is allowed. Then the
				//midpoint rule at the ramp's kinks, at most (perimeter / 8 w_px)
				//pixels^2, the outer kink's perimeter grown by the ramp, and
				//1e-4 for float32 summed over the plane.
				const double rampPx = c.near ? wg * D * l.tanAlpha / pix : wp * D / tStar / pix;
				const double areaPx = o.area / ( pix * pix ), perimPx = o.perimeter / pix * ( c.near ? 1.0 + wg : 1.0 + wp / ( 0.5 * kA ) );
				const double curve  = c.near ? 2.0 * ( wg * l.tanAlpha ) * ( wg * l.tanAlpha ) : 2.0 * ( wp / tStar ) * ( wp / tStar );
				const double bound  = curve + perimPx / ( 8.0 * std::max( rampPx, 0.5 ) ) / areaPx + 1e-4;
				const double rel    = got / ref - 1.0;
				if( b.sum > 0.0 && !b.edge )
					++measured;
				worstOverBound = worse( worstOverBound, std::fabs( rel ) / bound );
				detail += fmt( " %g m/%g deg %+.2e (bound %.1e);", D, place[ 1 ], rel, bound );
			}
			const double cosF = dot( -1.0 * ( c.near ? l.axis : normalise( X - lampPos ) ), n );
			Check( measured == places && assumed && worstOverBound <= 1.0,
			       fmt( "%dx%d %s field, beam %g deg, %g mm facet at %.1f deg incidence: intercepted %.4e lm;%s worst at %.2f of its bound%s", raster.w,
			            raster.h, c.near ? "near" : "far", c.beam, 1000.0 * c.facet, deg( std::acos( std::min( 1.0, cosF ) ) ), ref, detail.c_str(),
			            worstOverBound, assumed ? "" : " -- THE QUADRATURE'S ASSUMPTION FAILS" ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --circles: the lamp on the axis; every spot rides a circle at the motor's
// rate, its radius set by the doubling of the latitude.
//===========================================================================
/// The least-squares slope of y on x.
double slope( const std::vector< double >& x, const std::vector< double >& y )
{
	double mx = 0, my = 0;
	for( size_t i = 0; i < x.size(); ++i )
	{
		mx += x[ i ];
		my += y[ i ];
	}
	mx /= x.size();
	my /= y.size();
	double sxy = 0, sxx = 0;
	for( size_t i = 0; i < x.size(); ++i )
	{
		sxy += ( x[ i ] - mx ) * ( y[ i ] - my );
		sxx += ( x[ i ] - mx ) * ( x[ i ] - mx );
	}
	return sxx > 0.0 ? sxy / sxx : 0.0;
}

/// A circle through points (Kasa's algebraic fit).
bool fitCircle( const std::vector< std::array< double, 2 > >& pts, double& cx, double& cy, double& r )
{
	double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0, sz = 0;
	const double n = static_cast< double >( pts.size() );
	for( const auto& p : pts )
	{
		const double x = p[ 0 ], y = p[ 1 ], z = x * x + y * y;
		sx += x;
		sy += y;
		sxx += x * x;
		syy += y * y;
		sxy += x * y;
		sxz += x * z;
		syz += y * z;
		sz += z;
	}
	//Solve [ sxx sxy sx; sxy syy sy; sx sy n ] [ A B C ] = [ sxz syz sz ] for x^2 + y^2 = A x + B y + C.
	double m[ 3 ][ 4 ] = { { sxx, sxy, sx, sxz }, { sxy, syy, sy, syz }, { sx, sy, n, sz } };
	for( int col = 0; col < 3; ++col )
	{
		int pivot = col;
		for( int row = col + 1; row < 3; ++row )
			if( std::fabs( m[ row ][ col ] ) > std::fabs( m[ pivot ][ col ] ) )
				pivot = row;
		for( int c = 0; c < 4; ++c )
			std::swap( m[ col ][ c ], m[ pivot ][ c ] );
		if( std::fabs( m[ col ][ col ] ) < 1e-300 )
			return false;
		for( int row = 0; row < 3; ++row )
			if( row != col )
			{
				const double f = m[ row ][ col ] / m[ col ][ col ];
				for( int c = 0; c < 4; ++c )
					m[ row ][ c ] -= f * m[ col ][ c ];
			}
	}
	const double A = m[ 0 ][ 3 ] / m[ 0 ][ 0 ], B = m[ 1 ][ 3 ] / m[ 1 ][ 1 ], C = m[ 2 ][ 3 ] / m[ 2 ][ 2 ];
	cx = 0.5 * A;
	cy = 0.5 * B;
	r  = std::sqrt( C + cx * cx + cy * cy );
	return true;
}

/// The light-weighted centroid of one facet's spot on a plane, from the
/// rays themselves: a polar grid over the pupil, through a square grid over
/// the facet (or its centre alone, grid 1), reflected, each weighted by the
/// light it carries (cos at the facet x cos at the pupil / d^2). The gate
/// is taken as hard (a ray outside it carries nothing).
Vec3 spotCentroid( const lamp::Lamp& l, const ball::Ring& ring, int index, double radius, double facet, int grid, const Vec3& planeC, const Vec3& planeN )
{
	const Vec3 n = ball::NormalOf( ring, index, 0.0 );
	Vec3 east, north;
	ball::EdgesOf( ring, index, 0.0, east, north );
	const Vec3 X = radius * n;
	const int nr = 100, nt = 100;
	Vec3 sum     = { 0.0, 0.0, 0.0 };
	double total = 0.0;
	for( int a = 0; a < grid; ++a )
		for( int b = 0; b < grid; ++b )
		{
			const double ea = grid == 1 ? 0.0 : facet * ( ( a + 0.5 ) / grid - 0.5 ), eb = grid == 1 ? 0.0 : facet * ( ( b + 0.5 ) / grid - 0.5 );
			const Vec3 Y    = X + ea * east + eb * north;
			for( int ir = 0; ir < nr; ++ir )
				for( int it = 0; it < nt; ++it )
				{
					const double rr = 0.5 * kA * ( ir + 0.5 ) / nr, th = 2.0 * kPi * ( it + 0.5 ) / nt;
					const Vec3 q    = l.position + rr * ( std::cos( th ) * l.right + std::sin( th ) * l.up );
					const Vec3 d    = Y - q;
					const double dd = length( d );
					const Vec3 w    = ( 1.0 / dd ) * d;
					const double cq = dot( w, l.axis ), cf = -dot( w, n );
					if( cf <= 0.0 || std::hypot( dot( w, l.right ), dot( w, l.up ) ) > cq * l.tanAlpha )
						continue;
					Vec3 at;
					if( !hitPlane( Y, reflect( w, n ), planeC, planeN, at ) )
						continue;
					const double weight = cf * cq / ( dd * dd ) * rr;
					sum                 = sum + weight * at;
					total += weight;
				}
		}
	return total > 0.0 ? ( 1.0 / total ) * sum : sum;
}

int runCircles( const Perturb& perturb )
{
	std::printf( "\n=== circles: the lamp below on the axis; each ring's spot circles the axis foot at the motor's rate, its radius from the doubled latitude\n" );
	const double diameter = 0.5, facet = 0.01, beam = 8.0, ceiling = 1.5, rpm = 5.0;
	const Vec3 lampPos        = { 0.0, -5.0, 0.0 };
	const ball::Layout layout = layoutFor( diameter, facet );
	const double omega        = 2.0 * kPi * SpeedFromParam( ParamFromSpeed( rpm ) ) / 60.0;//rad/s
	for( const Raster& raster : kRasters )
	{
		double worstOverBound = 0.0;
		int measured          = 0;
		std::string detail;
		for( double latitude : { -8.0, -16.0, -24.0 } )
		{
			const int k             = ball::NearestRing( layout, rad( latitude ) );
			const ball::Ring& ring  = layout.rings[ static_cast< size_t >( k ) ];
			//The radius the centre ray predicts: exact, in double.
			const Vec3 n0           = ball::NormalOf( ring, 0, 0.0 );
			const Vec3 X0           = layout.radius * n0;
			const Vec3 r0           = reflect( normalise( X0 - lampPos ), n0 );
			const Vec3 c0           = X0 + ( ( ceiling - X0[ 1 ] ) / r0[ 1 ] ) * r0;
			const double rhoPred    = std::hypot( c0[ 0 ], c0[ 2 ] );
			const double phi        = ring.phi;
			const double rhoIdeal   = layout.radius * std::cos( phi ) + ( ceiling - layout.radius * std::sin( phi ) ) * std::tan( 2.0 * std::fabs( phi ) );
			//What the picture's centroid should be: the light-weighted centroid
			//of the rays (near grazing, cos theta_f varies across the pupil
			//and pulls the centroid off the centre ray). The facet's own size
			//moves it by `smear`, which the shader's ramp models only to
			//second order: that is allowed for.
			const lamp::Lamp lp     = specLamp( lampPos, beam );
			const Vec3 ceilC        = { 0.0, ceiling, 0.0 }, ceilN = { 0.0, -1.0, 0.0 };
			const Vec3 cLight       = spotCentroid( lp, ring, 0, layout.radius, layout.facet, 1, ceilC, ceilN );
			const Vec3 cFacet       = spotCentroid( lp, ring, 0, layout.radius, layout.facet, 6, ceilC, ceilN );
			const double rhoLight   = std::hypot( cLight[ 0 ], cLight[ 2 ] );
			const double smear      = std::fabs( std::hypot( cFacet[ 0 ], cFacet[ 2 ] ) - rhoLight );

			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			opticsControls( rig, beam, diameter, facet );
			rig.Set( PT_SPEED, ParamFromSpeed( rpm ) );
			TestRig t    = opticsRig( lampPos, k, 0, perturb.kMax );
			t.psi        = false;
			t.plane      = true;
			t.centre     = { 0.0, ceiling, 0.0 };
			t.normal     = { 0.0, -1.0, 0.0 };
			t.upHint     = upHintFor( t.normal );
			t.halfHeight = 1.25 * rhoPred;
			rig.plugin.SetRigForTest( t );
			rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookRaw | shaders::kHookSpotsOnly );
			const PlaneFrame f = frameOf( t, raster.w, raster.h );
			std::vector< std::array< double, 2 > > pts;
			std::vector< double > times, angles;
			bool ok        = true;
			double bound   = 0.0;
			for( int frame = 0; frame <= 180; ++frame )
			{
				if( !rig.Render( 1 ) )
					return 1;
				if( frame % 10 != 0 )
					continue;
				const Floats px = rig.Output();
				const Blob b    = weighted( px, raster.w, raster.h );
				if( !( b.sum > 0.0 ) || b.edge )
				{
					ok = false;
					continue;
				}
				const Vec3 c = f.worldOf( b.cx, b.cy );
				pts.push_back( { c[ 0 ], c[ 2 ] } );
				times.push_back( rig.plugin.HostNow() );
				angles.push_back( std::atan2( -c[ 2 ], c[ 0 ] ) );
				//The ramp on the ceiling: the pupil's ramp projected, at least.
				const double D  = length( c - X0 );
				const double wp = 0.5 * layout.facet * ( D + 5.0 ) / 5.0;
				bound           = std::max( bound, centroidBound( wp / f.pixel() ) * f.pixel() );
			}
			double cx = 0.0, cy = 0.0, rho = 0.0;
			if( !ok || pts.size() < 10 || !fitCircle( pts, cx, cy, rho ) )
			{
				worstOverBound = worse( worstOverBound, std::numeric_limits< double >::quiet_NaN() );
				detail += fmt( " ring %d: no spot;", k );
				continue;
			}
			++measured;
			for( size_t i = 1; i < angles.size(); ++i )
				while( angles[ i ] - angles[ i - 1 ] < -kPi )
					angles[ i ] += 2.0 * kPi;
			const double rate = slope( times, angles );
			const double span = angles.back() - angles.front();
			//The bound: two centroids' midpoint-rule bounds, the facet's smear,
			//and 1e-5 m for the 100 x 100 quadrature.
			const double tolC  = 2.0 * bound + smear + 1e-5;
			const double tolW  = 2.0 * ( bound / rhoPred ) / std::fabs( span ) + 1e-6;
			worstOverBound     = worse( worstOverBound, std::hypot( cx, cy ) / tolC );
			worstOverBound     = worse( worstOverBound, std::fabs( rho - rhoLight ) / tolC );
			worstOverBound     = worse( worstOverBound, std::fabs( rate / omega - 1.0 ) / tolW );
			detail += fmt( "\n      ring %d (latitude %.2f deg): centre off the axis by %.2e m; radius %.5f m (the rays' light-weighted centroid %.5f, bound "
			               "%.1e; centre ray %.4f, ideal doubling law %.4f); rate %.6f of the motor's (bound %.1e); speed %.3f m/s",
			               k, deg( phi ), std::hypot( cx, cy ), rho, rhoLight, tolC, rhoPred, rhoIdeal, rate / omega, tolW, rate * rho );
		}
		Check( measured == 3 && worstOverBound <= 1.0,
		       fmt( "%dx%d, 5 rpm, a quarter turn, three rings:%s\n      worst at %.2f of its bound", raster.w, raster.h, detail.c_str(), worstOverBound ) );
	}
	return Verdict();
}

//===========================================================================
// --window: what a spot carries. Far from the lamp, a window of the gobo
// (radius (A/2)/(L tan alpha) around the facet's place in the beam); near
// it, the whole gobo; and either way, mirrored.
//===========================================================================
/// A clip whose red is u and green v (as linear light), across the frame.
Floats uvCard( int w, int h )
{
	Floats card( static_cast< size_t >( w ) * h * 4 );
	auto code = []( double v ) { return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow( v, 1.0 / 2.4 ) - 0.055; };
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			float* o = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			o[ 0 ]   = static_cast< float >( code( ( x + 0.5 ) / w ) );
			o[ 1 ]   = static_cast< float >( code( ( y + 0.5 ) / h ) );
			o[ 2 ]   = 0.0f;
			o[ 3 ]   = 1.0f;
		}
	return card;
}

/// The gobo coordinate decoded from the uv card through the gobo pass's
/// centre crop (aspect >= 1).
void decodeG( const float* px, double aspect, double& gx, double& gy )
{
	gx = ( px[ 0 ] - 0.5 ) * 2.0 * aspect;
	gy = ( px[ 1 ] - 0.5 ) * 2.0;
}

/// The least-squares map from plane metres to g over the pixels with
/// coverage >= level; returns the handedness of the picture against the
/// direction of travel: the sign of det[ dP/dgx, dP/dgy, travel ].
double handedness( const Floats& px, const PlaneFrame& f, double aspect, double level, const Vec3& travel, int& used )
{
	double s[ 3 ][ 3 ] = {}, tx[ 3 ] = {}, ty[ 3 ] = {};
	used = 0;
	for( int y = 0; y < f.h; ++y )
		for( int x = 0; x < f.w; ++x )
		{
			const float* p = &px[ ( static_cast< size_t >( y ) * f.w + x ) * 4 ];
			if( p[ 3 ] < level )
				continue;
			double gx, gy, mx, my;
			decodeG( p, aspect, gx, gy );
			f.planeOf( f.worldOf( x + 0.5, y + 0.5 ), mx, my );
			const double v[ 3 ] = { mx, my, 1.0 };
			for( int a = 0; a < 3; ++a )
			{
				for( int b = 0; b < 3; ++b )
					s[ a ][ b ] += v[ a ] * v[ b ];
				tx[ a ] += v[ a ] * gx;
				ty[ a ] += v[ a ] * gy;
			}
			++used;
		}
	if( used < 10 )
		return 0.0;
	auto solve = [ & ]( const double rhs[ 3 ], double out[ 3 ] ) {
		double m[ 3 ][ 4 ];
		for( int a = 0; a < 3; ++a )
		{
			for( int b = 0; b < 3; ++b )
				m[ a ][ b ] = s[ a ][ b ];
			m[ a ][ 3 ] = rhs[ a ];
		}
		for( int c = 0; c < 3; ++c )
			for( int r = 0; r < 3; ++r )
				if( r != c )
				{
					const double k = m[ r ][ c ] / m[ c ][ c ];
					for( int q = 0; q < 4; ++q )
						m[ r ][ q ] -= k * m[ c ][ q ];
				}
		for( int a = 0; a < 3; ++a )
			out[ a ] = m[ a ][ 3 ] / m[ a ][ a ];
	};
	double cx[ 3 ], cy[ 3 ];
	solve( tx, cx );
	solve( ty, cy );
	//J = [ dgx/dmx dgx/dmy; dgy/dmx dgy/dmy ]; its inverse's columns are
	//dP/dgx and dP/dgy in plane coordinates.
	const double det = cx[ 0 ] * cy[ 1 ] - cx[ 1 ] * cy[ 0 ];
	if( det == 0.0 )
		return 0.0;
	const double i00 = cy[ 1 ] / det, i01 = -cx[ 1 ] / det, i10 = -cy[ 0 ] / det, i11 = cx[ 0 ] / det;
	const Vec3 dPx = i00 * f.view.right + i10 * f.view.up;
	const Vec3 dPy = i01 * f.view.right + i11 * f.view.up;
	return dot( cross( dPx, dPy ), travel ) > 0.0 ? 1.0 : -1.0;
}

int runWindow( const Perturb& perturb )
{
	std::printf( "\n=== window: far from the lamp a spot carries a window of the gobo, (A/2)/(L tan alpha) round the facet's place in the beam; near it, all of "
	             "it; reflected, it is mirrored\n" );
	const double diameter = 0.5, facet = 0.01, beam = 5.0;
	const ball::Layout layout = layoutFor( diameter, facet );
	const Vec3 toLamp         = normalise( Vec3 { 0.3, 0.4, 1.0 } );
	for( const Raster& raster : kRasters )
	{
		const double aspect = static_cast< double >( raster.w ) / raster.h;
		const Floats card   = uvCard( raster.w, raster.h );
		auto overRig        = [ & ]( Rig& rig ) {
			if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
				return false;
			opticsControls( rig, beam, diameter, facet );
			rig.Set( PT_CLIP_AS, static_cast< float >( ClipAs::Gobo ) );
			rig.Set( PT_MIX, 1.0f );
			return true;
		};
		//Far field: two facets, one on the axis and one off it.
		{
			const Vec3 lampPos = 5.0 * toLamp;
			const lamp::Lamp l = specLamp( lampPos, beam );
			double worstOverBound = 0.0;
			int measured          = 0;
			std::string detail;
			for( double gamma : { 0.0, 18.0 } )
			{
				int k = 0, j = 0;
				facetNearest( layout, tilted( toLamp, rad( gamma ) ), k, j );
				const Vec3 n    = ball::NormalOf( layout.rings[ static_cast< size_t >( k ) ], j, 0.0 );
				const Vec3 X    = layout.radius * n;
				const Vec3 axis = reflect( normalise( X - lampPos ), n );
				const double Lx = dot( X - lampPos, l.axis );
				const double gcx = dot( X - lampPos, l.right ) / ( Lx * l.tanAlpha ), gcy = dot( X - lampPos, l.up ) / ( Lx * l.tanAlpha );
				Outline o;
				TestRig t = framePlane( opticsRig( lampPos, k, j, perturb.kMax ), X, axis, 2.0, 0.0, pupilRim( l, X, n ), 1.4, raster.w, raster.h, o );
				Rig rig( true );
				if( !overRig( rig ) )
					return 1;
				const Floats px    = shootPlane( rig, t, t.centre, t.normal, t.halfHeight, perturb.hooks | shaders::kHookGoboOut | shaders::kHookSpotsOnly );
				const PlaneFrame f = frameOf( t, raster.w, raster.h );
				//The ring of pixels about the 0.5 contour: their g, about the
				//centre the interior's g gives.
				double sx = 0, sy = 0, n1 = 0, sr = 0, n2 = 0;
				bool edge = false;
				for( int y = 0; y < raster.h; ++y )
					for( int x = 0; x < raster.w; ++x )
					{
						const float* p = &px[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
						if( p[ 3 ] > 0.0f && ( x < 2 || y < 2 || x >= raster.w - 2 || y >= raster.h - 2 ) )
							edge = true;
						if( p[ 3 ] >= 0.5f )
						{
							double gx, gy;
							decodeG( p, aspect, gx, gy );
							sx += gx;
							sy += gy;
							n1 += 1.0;
						}
					}
				const double mx = n1 > 0 ? sx / n1 : 0.0, my = n1 > 0 ? sy / n1 : 0.0;
				for( int y = 0; y < raster.h; ++y )
					for( int x = 0; x < raster.w; ++x )
					{
						const float* p = &px[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
						if( p[ 3 ] >= 0.3f && p[ 3 ] <= 0.7f )
						{
							double gx, gy;
							decodeG( p, aspect, gx, gy );
							sr += std::hypot( gx - mx, gy - my );
							n2 += 1.0;
						}
					}
				const double radius = n2 > 0 ? sr / n2 : 0.0;
				//The bound: the contour pixels sit inside the ramp's middle 40%
				//(+-0.2 of the ramp's full width in g), plus half a pixel, plus
				//the gobo's half-float u (2^-12 near 0.5, times 2 aspect).
				const double D      = 2.0;
				const double wpG    = ( 0.5 * layout.facet * ( D + Lx ) / D + 0.5 * f.pixel() * Lx / D ) / ( Lx * l.tanAlpha );
				const double pixG   = f.pixel() / ( D * l.tanAlpha );
				const double quant  = std::ldexp( 1.0, -12 ) * 2.0 * aspect;
				const double expect = rampEdgeRef( 0.5 * kA, wpG * Lx * l.tanAlpha ) / ( Lx * l.tanAlpha );
				const double tolR   = 0.4 * wpG + 0.5 * pixG + quant;
				const double tolC   = 0.5 * pixG + quant + wpG * wpG;
				if( n1 > 10 && n2 > 10 && !edge )
					++measured;
				worstOverBound = worse( worstOverBound, std::fabs( radius - expect ) / tolR );
				worstOverBound = worse( worstOverBound, std::hypot( mx - gcx, my - gcy ) / tolC );
				detail += fmt( " facet at %g deg: window radius %.4f (expected %.4f, bound %.4f), centre (%.3f, %.3f) (expected (%.3f, %.3f));", gamma, radius,
				               expect, tolR, mx, my, gcx, gcy );
			}
			Check( measured == 2 && worstOverBound <= 1.0,
			       fmt( "%dx%d far field (5 m, beam 5 deg):%s worst at %.2f of its bound", raster.w, raster.h, detail.c_str(), worstOverBound ) );
		}
		//Near field: the whole gobo, mirrored; the spill (the direct
		//projection) not.
		{
			const Vec3 lampPos = 0.7 * toLamp;
			const lamp::Lamp l = specLamp( lampPos, beam );
			int k = 0, j = 0;
			facetNearest( layout, toLamp, k, j );
			const Vec3 n    = ball::NormalOf( layout.rings[ static_cast< size_t >( k ) ], j, 0.0 );
			const Vec3 X    = layout.radius * n;
			const Vec3 axis = reflect( l.axis, n );
			Outline o;
			TestRig t = framePlane( opticsRig( lampPos, k, j, perturb.kMax ), X, axis, 1.5, 0.0, gateCone( axis, l.alpha ), 1.25, raster.w, raster.h, o );
			Rig rig( true );
			if( !overRig( rig ) )
				return 1;
			const Floats px    = shootPlane( rig, t, t.centre, t.normal, t.halfHeight, perturb.hooks | shaders::kHookGoboOut | shaders::kHookSpotsOnly );
			const PlaneFrame f = frameOf( t, raster.w, raster.h );
			double maxG = 0.0;
			int inside  = 0;
			for( size_t i = 0; i < px.size(); i += 4 )
				if( px[ i + 3 ] >= 0.95f )
				{
					double gx, gy;
					decodeG( &px[ i ], aspect, gx, gy );
					maxG = std::max( maxG, std::hypot( gx, gy ) );
					++inside;
				}
			const double wg    = 0.5 * ( layout.facet + f.pixel() ) / ( 1.5 * l.tanAlpha );
			const double pixG  = f.pixel() / ( 1.5 * l.tanAlpha );
			const double lowG  = rampEdgeRef( 1.0, wg ) - 2.0 * 0.45 * wg - pixG - 4.0 / shaders::kGoboTexelsPerUnit;
			int usedR          = 0;
			const double handR = handedness( px, f, aspect, 0.9, axis, usedR );

			//The spill: no facets, the beam widened past the ball onto a plane
			//behind it.
			const Vec3 farLamp  = 4.0 * toLamp;
			const lamp::Lamp lf = specLamp( farLamp, 12.0 );
			TestRig ts          = opticsRig( farLamp, -2, -2, perturb.kMax );
			Rig spillRig( true );
			if( !overRig( spillRig ) )
				return 1;
			spillRig.Set( PT_BEAM, ParamFromBeam( 12.0 ) );
			const double behind = 3.0;
			const double half   = 1.1 * ( 0.5 * kA + ( 4.0 + behind ) * lf.tanAlpha );
			const Floats sp     = shootPlane( spillRig, ts, behind * lf.axis, -1.0 * lf.axis, half, perturb.hooks | shaders::kHookGoboOut );
			ts.plane            = true;
			ts.centre           = behind * lf.axis;
			ts.normal           = -1.0 * lf.axis;
			ts.upHint           = upHintFor( ts.normal );
			ts.halfHeight       = half;
			int usedS           = 0;
			const double handS  = handedness( sp, frameOf( ts, raster.w, raster.h ), aspect, 0.9, lf.axis, usedS );
			Check( inside > 50 && maxG >= lowG && maxG <= 1.0 + pixG && handR > 0.0 && handS < 0.0,
			       fmt( "%dx%d near field (0.70 m): the spot's interior reaches |g| = %.4f of the gobo (all of it: >= %.4f, <= 1); reflected handedness %+g "
			            "(mirrored: +1) over %d pixels, the spill's %+g (direct: -1) over %d",
			            raster.w, raster.h, maxG, lowG, handR, inside, handS, usedS ) );
			(void)usedR;
		}
	}
	return Verdict();
}

//===========================================================================
// --missing: the same facets missing everywhere, and the light that goes.
//===========================================================================
double sumOf( const Floats& px, int channel = 1 )
{
	double s = 0.0;
	for( size_t i = static_cast< size_t >( channel ); i < px.size(); i += 4 )
		s += px[ i ];
	return s;
}

/// Where each lit facet's centre ray (the pupil's centre, through the facet's
/// centre, reflected) lands on the frame's view plane, in pixels, with the
/// spot's radius there and its weight (the cosine its flux carries).
struct Landing
{
	int k = 0, j = 0;
	double px = 0.0, py = 0.0, radiusPx = 0.0, weight = 0.0, gc = 0.0;
};

std::vector< Landing > landings( const FrameData& fd, int w, int h )
{
	std::vector< Landing > out;
	const room::View& v = fd.view;
	const double halfW  = v.halfHeight * w / h;
	const double pix    = 2.0 * v.halfHeight / h;
	for( int k = 0; k < fd.layout.Rings(); ++k )
		for( int j = 0; j < fd.layout.rings[ static_cast< size_t >( k ) ].count; ++j )
		{
			const Vec3 n   = ball::NormalOf( fd.layout.rings[ static_cast< size_t >( k ) ], j, fd.psi );
			const Vec3 X   = fd.layout.radius * n;
			const Vec3 win = normalise( X - fd.lamp.position );
			const double c = -dot( win, n );
			if( c <= 0.0 )
				continue;
			const double cq = dot( win, fd.lamp.axis );
			const double gc = std::hypot( dot( win, fd.lamp.right ), dot( win, fd.lamp.up ) ) / ( cq * fd.lamp.tanAlpha );
			if( gc > 1.0 )
				continue;
			const Vec3 r = reflect( win, n );
			Vec3 at;
			if( !hitPlane( X, r, v.centre, v.normal, at ) )
				continue;
			const double x = dot( at - v.centre, v.right ), y = dot( at - v.centre, v.up );
			if( std::fabs( x ) > halfW || std::fabs( y ) > v.halfHeight )
				continue;
			const double L    = length( X - fd.lamp.position ), D = length( at - X );
			const double cosW = std::max( std::fabs( dot( r, v.normal ) ), 0.3 );
			Landing l;
			l.k        = k;
			l.j        = j;
			l.px       = ( x / halfW + 1.0 ) * 0.5 * w;
			l.py       = ( y / v.halfHeight + 1.0 ) * 0.5 * h;
			l.radiusPx = ( ( 0.5 * kA + fd.layout.facet ) * D / L + fd.layout.facet ) / cosW / pix;
			l.weight   = c;
			l.gc       = gc;
			out.push_back( l );
		}
	return out;
}

int runMissing( const Perturb& perturb )
{
	std::printf( "\n=== missing: the same seed loses the same facets in every instance, after re-initialising and 120 frames later, and they are the facets "
	             "the CPU's hash names; the light lost is the fraction missing\n" );
	const double fraction = 0.3;
	//The wall at the defaults sees only facets near the gate's edge (the
	//lamp is off to the side), so the per-facet and light checks look at a
	//plane square to the lamp's direction, 3 m out, the lamp 5 m out.
	const Vec3 toLamp = normalise( Vec3 { 0.3, 0.4, 1.0 } );
	auto setup = [ & ]( Rig& rig, double missing, int seed, int extraHooks, bool facing ) {
		rig.Set( PT_VIEW, static_cast< float >( View::Wall ) );
		rig.Set( PT_MISSING, ParamFromMissing( missing ) );
		rig.Set( PT_SEED, static_cast< float >( seed ) );
		rig.Set( PT_SPEED, ParamFromSpeed( 0.0 ) );
		rig.Set( PT_MOTOR, static_cast< float >( Motor::Free ) );
		rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookRaw | shaders::kHookSpotsOnly | extraHooks );
		TestRig t;
		t.kMax = perturb.kMax;
		if( facing )
		{
			t.lamp       = true;
			t.lampPos    = 5.0 * toLamp;
			t.plane      = true;
			t.centre     = 3.0 * toLamp;
			t.normal     = -1.0 * toLamp;
			t.upHint     = upHintFor( t.normal );
			t.halfHeight = 2.0;
		}
		rig.plugin.SetRigForTest( t );
	};
	auto rel = []( const Floats& x, const Floats& y ) {
		double d = 0.0, s = 0.0;
		for( size_t i = 0; i < x.size(); ++i )
		{
			d += std::fabs( x[ i ] - y[ i ] );
			s += std::fabs( y[ i ] );
		}
		return s > 0.0 ? d / s : 1.0;
	};
	for( const Raster& r : kRasters )
	{
		//The same facets, everywhere.
		Rig a, b, other;
		if( !startRig( a, r.w, r.h, perturb ) || !startRig( b, r.w, r.h, perturb ) || !startRig( other, r.w, r.h, perturb ) )
			return 1;
		setup( a, fraction, 1, 0, false );
		setup( b, fraction, 1, 0, false );
		setup( other, fraction, 2, 0, false );
		if( !a.Render( 1 ) || !b.Render( 1 ) || !other.Render( 1 ) )
			return 1;
		const Floats pa = a.Output(), pb = b.Output(), po = other.Output();
		a.plugin.DeInitGL();
		FFGLViewportStruct vp = {};
		vp.width              = static_cast< FFUInt32 >( r.w );
		vp.height             = static_cast< FFUInt32 >( r.h );
		if( a.plugin.InitGL( &vp ) != FF_SUCCESS || !a.Render( 1 ) )
			return 1;
		const Floats pr = a.Output();
		if( !a.Render( 120 ) )
			return 1;
		const Floats pl = a.Output();
		//One context, the same floats; the software renderer under load has
		//been seen to differ in the last bits, so 1e-5 of the picture's sum.
		const double twin = rel( pb, pa ), reinit = rel( pr, pa ), later = rel( pl, pa ), seed = rel( po, pa );
		Check( twin <= 1e-5 && reinit <= 1e-5 && later <= 1e-5 && seed > 1e-2,
		       fmt( "%dx%d, Missing 30%%, the wall: a twin differs by %.1e, re-initialised by %.1e, 120 frames on by %.1e (all <= 1e-5 of the picture); "
		            "Seed 2 by %.3f",
		            r.w, r.h, twin, reinit, later, seed ) );

		//The facets the CPU's hash names: every isolated spot, sampled where
		//its centre ray lands, is dark in the Missing picture exactly when the
		//CPU says it is missing.
		{
			Rig full, gone;
			if( !startRig( full, r.w, r.h, perturb ) || !startRig( gone, r.w, r.h, perturb ) )
				return 1;
			setup( full, 0.0, 1, shaders::kHookCoverage, true );
			setup( gone, fraction, 1, shaders::kHookCoverage, true );
			if( !full.Render( 1 ) || !gone.Render( 1 ) )
				return 1;
			const Floats pf = full.Output(), pg = gone.Output();
			const FrameData& fd = gone.plugin.LastFrame();
			const std::vector< Landing > all = landings( fd, r.w, r.h );
			int sampled = 0, missingCpu = 0, wrong = 0, unresolved = 0;
			for( size_t i = 0; i < all.size(); ++i )
			{
				const Landing& l = all[ i ];
				if( l.gc > 0.6 || l.px < l.radiusPx + 2.0 || l.py < l.radiusPx + 2.0 || l.px > r.w - l.radiusPx - 2.0 || l.py > r.h - l.radiusPx - 2.0 )
					continue;
				bool isolated = true;
				for( size_t o = 0; o < all.size() && isolated; ++o )
					if( o != i && std::hypot( all[ o ].px - l.px, all[ o ].py - l.py ) < 2.0 * ( l.radiusPx + all[ o ].radiusPx ) )
						isolated = false;
				if( !isolated )
					continue;
				const size_t at   = ( static_cast< size_t >( l.py ) * r.w + static_cast< size_t >( l.px ) ) * 4 + 1;
				const bool cpu    = ball::IsMissing( l.k, l.j, fd.seed, fd.missingBelow );
				++sampled;
				missingCpu += cpu;
				if( !( pf[ at ] > 0.05 ) )
					++unresolved;
				else if( cpu ? pg[ at ] != 0.0f : !( pg[ at ] > 0.05 ) )
					++wrong;
			}
			Check( sampled >= 20 && unresolved == 0 && wrong == 0 && missingCpu > 0,
			       fmt( "%dx%d, a plane facing the lamp: %d isolated spots sampled where their centre rays land, %d of them missing by the CPU's hash; the GPU disagrees at %d, and %d "
			            "were not lit at Missing 0",
			            r.w, r.h, sampled, missingCpu, wrong, unresolved ) );
		}

		//The light lost: the 1 m ball with 5 mm facets (thousands on the
		//wall), against binomial counting weighted by each facet's cosine.
		{
			Rig full, gone;
			if( !startRig( full, r.w, r.h, perturb ) || !startRig( gone, r.w, r.h, perturb ) )
				return 1;
			for( Rig* rig : { &full, &gone } )
			{
				rig->Set( PT_DIAMETER, ParamFromDiameter( 1.0 ) );
				rig->Set( PT_FACET_SIZE, ParamFromFacet( 0.005 ) );
			}
			setup( full, 0.0, 1, 0, true );
			setup( gone, fraction, 1, 0, true );
			if( !full.Render( 1 ) || !gone.Render( 1 ) )
				return 1;
			const std::vector< Landing > all = landings( gone.plugin.LastFrame(), r.w, r.h );
			double sw = 0.0, sw2 = 0.0;
			for( const Landing& l : all )
			{
				sw += l.weight;
				sw2 += l.weight * l.weight;
			}
			const double ratio = sumOf( gone.Output() ) / sumOf( full.Output() );
			const double sigma = sw > 0.0 ? std::sqrt( fraction * ( 1.0 - fraction ) * sw2 ) / sw : 1.0;
			Check( all.size() > 500 && std::fabs( ratio - ( 1.0 - fraction ) ) <= 3.0 * sigma,
			       fmt( "%dx%d, a plane facing the lamp, 1 m ball, 5 mm facets: light kept %.4f against 0.7000 +- %.4f (3 sd of %zu facets on the wall, weighted)", r.w, r.h, ratio,
			            3.0 * sigma, all.size() ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --rate, --clock, --sync: the motor, through the plugin.
//===========================================================================
int runRate( const Perturb& perturb )
{
	std::printf( "\n=== rate: Free at 1 rpm is in the same place after 4 s at 24, 30, 60 and 240 fps, and so is a facet's spot\n" );
	const Raster raster = kRasters.back();
	//SPEC-mirrorball.md: Speed is +-5 rpm, linear, so 1 rpm is the host
	//value 0.6, written here rather than through ParamFromSpeed (a range
	//slipped in Controls.cpp would round-trip unnoticed); a float, so off by
	//at most half an ulp, at 10 rpm a unit.
	const float param   = 0.6f;
	const double tolRpm = 10.0 * std::ldexp( 1.0, std::ilogb( param ) - 24 );
	double lo = 1e30, hi = -1e30;
	std::string detail;
	Vec3 spot[ 2 ];
	int shot = 0;
	for( double fps : { 24.0, 30.0, 60.0, 240.0 } )
	{
		Rig rig;
		rig.fps = fps;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_MOTOR, static_cast< float >( Motor::Free ) );
		rig.Set( PT_SPEED, param );
		const int frames = static_cast< int >( std::lround( 4.0 * fps ) ) + 1;
		const bool picture = fps == 30.0 || fps == 240.0;
		if( picture )
		{
			//One facet on a plane square to its reflection, the motor running.
			opticsControls( rig, 8.0, 0.5, 0.01 );
			rig.Set( PT_SPEED, param );
			const ball::Layout layout = layoutFor( 0.5, 0.01 );
			const Vec3 lampPos        = { 0.0, -5.0, 0.0 };
			const int k               = ball::NearestRing( layout, rad( -16.0 ) );
			TestRig t                 = opticsRig( lampPos, k, 0, perturb.kMax );
			t.psi                     = false;
			t.plane                   = true;
			t.centre                  = { 0.0, 1.5, 0.0 };
			t.normal                  = { 0.0, -1.0, 0.0 };
			t.upHint                  = upHintFor( t.normal );
			t.halfHeight              = 1.6;
			rig.plugin.SetRigForTest( t );
			rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookRaw | shaders::kHookSpotsOnly );
		}
		if( !rig.Render( frames ) )
			return 1;
		const double psi = rig.plugin.MotorForTest().Unwrapped();
		lo               = std::min( lo, psi );
		hi               = std::max( hi, psi );
		if( std::isnan( psi ) )
			lo = std::numeric_limits< double >::quiet_NaN();
		detail += fmt( " %g fps %.12f;", fps, psi );
		if( picture )
		{
			TestRig t;
			t.plane       = true;
			t.centre      = { 0.0, 1.5, 0.0 };
			t.normal      = { 0.0, -1.0, 0.0 };
			t.upHint      = upHintFor( t.normal );
			t.halfHeight  = 1.6;
			const Blob b  = weighted( rig.Output(), raster.w, raster.h );
			spot[ shot++ ] = frameOf( t, raster.w, raster.h ).worldOf( b.cx, b.cy );
		}
	}
	const double apart = length( spot[ 1 ] - spot[ 0 ] );
	const double off   = std::max( std::fabs( lo - 4.0 / 60.0 ), std::fabs( hi - 4.0 / 60.0 ) );
	const double tolPsi = 4.0 / 60.0 * tolRpm;
	Check( hi - lo <= 1e-9 && off <= tolPsi && apart <= 1e-3 * 3.2 / raster.h,
	       fmt( "psi after 4 s:%s the rates agree to %.1e turns (1e-9), and with 1 rpm's 0.0666666667 to %.1e (the float control's half ulp: %.1e); the spot at "
	            "30 and 240 fps %.2e m apart (a thousandth of a pixel: %.1e)",
	            detail.c_str(), hi - lo, off, tolPsi, apart, 1e-3 * 3.2 / raster.h ) );
	return Verdict();
}

int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: at 499,000,000 ms the phase the shader gets still advances by rpm/60 x 1/60 a frame\n" );
	const Raster raster = kRasters.back();
	Rig rig;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return 1;
	rig.plugin.SetClockScaleForTest( 0.001 );
	rig.Set( PT_MOTOR, static_cast< float >( Motor::Free ) );
	rig.Set( PT_SPEED, ParamFromSpeed( 1.0 ) );
	const double rpm = SpeedFromParam( ParamFromSpeed( 1.0 ) );
	double worst = 0.0, last = -1.0;
	for( int f = 0; f < 20; ++f )
	{
		rig.plugin.SetTime( 499000000.0 + f * 1000.0 / 60.0 );
		glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
		glViewport( 0, 0, raster.w, raster.h );
		if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
			return 1;
		const double psi = rig.plugin.LastFrame().psi;
		if( last >= 0.0 && f > 1 )
		{
			double step = psi - last;
			step -= std::floor( step + 0.5 );
			worst = worse( worst, std::fabs( step - rpm / 60.0 / 60.0 ) );
		}
		last = psi;
	}
	Check( worst <= 1e-9, fmt( "20 frames at 499,000,000 ms: the phase's step off rpm/60/60 = %.3e turns by at most %.1e (1e-9)", rpm / 3600.0, worst ) );
	return Verdict();
}

/// The step's start, solved from one sample on its raised cosine:
/// psi = base + S ( 1 - cos( pi ( t - t0 ) / T ) ) / 2.
double stepStartFrom( double t, double psi, double base, double S, double T )
{
	const double x = std::clamp( 1.0 - 2.0 * ( psi - base ) / S, -1.0, 1.0 );
	return t - T / kPi * std::acos( x );
}

int runSync( const Perturb& perturb )
{
	std::printf( "\n=== sync: every step starts at the host's beat, found inside the frame, at 30 and 240 fps; the own clock keeps the last BPM\n" );
	const Raster raster = kRasters.back();
	const double bpm = 128.0, step = 15.0, glide = 0.5;
	const double S = StepAngleFromParam( ParamFromStepAngle( step ) ) / 360.0;
	const double T = GlideFromParam( static_cast< float >( glide ) ) * 60.0 / bpm;
	double worst   = 0.0;
	double at25[ 2 ] = {};
	int idx        = 0;
	std::string detail;
	for( double fps : { 30.0, 240.0 } )
	{
		Rig rig;
		rig.fps = fps;
		rig.bpm = bpm;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_MOTOR, static_cast< float >( Motor::Sync ) );
		rig.Set( PT_STEP_ON, static_cast< float >( StepOn::Beat ) );
		rig.Set( PT_STEP_ANGLE, ParamFromStepAngle( step ) );
		rig.Set( PT_GLIDE, static_cast< float >( glide ) );
		rig.Set( PT_SPEED, ParamFromSpeed( 1.0 ) );
		std::vector< std::pair< double, double > > samples;//( t, psi )
		const int frames = static_cast< int >( std::lround( 3.0 * fps ) );
		for( int f = 0; f < frames; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			samples.emplace_back( rig.plugin.HostNow(), rig.plugin.MotorForTest().Unwrapped() );
			if( f == static_cast< int >( std::lround( 2.5 * fps ) ) )
				at25[ idx ] = rig.plugin.MotorForTest().Unwrapped();
		}
		++idx;
		//Beats 1.. at b 60/bpm (the host's bar phase is 0 at t = 0). For each,
		//the sample nearest the middle of its glide.
		int solved = 0;
		double off = 0.0;
		for( int b = 1; b * 60.0 / bpm + T < 3.0; ++b )
		{
			const double tb = b * 60.0 / bpm;
			const auto* best = &samples.front();
			for( const auto& s : samples )
				if( std::fabs( s.first - ( tb + 0.5 * T ) ) < std::fabs( best->first - ( tb + 0.5 * T ) ) )
					best = &s;
			const double t0 = stepStartFrom( best->first, best->second, ( b - 1 ) * S, S, T );
			off             = worse( off, std::fabs( t0 - tb ) );
			++solved;
		}
		worst = worse( worst, off );
		detail += fmt( " %g fps: %d steps, each start off its beat by at most %.1e s;", fps, solved, off );
	}
	//The own clock: a BPM sent, the phase standing still.
	Rig own;
	if( !startRig( own, raster.w, raster.h, perturb ) )
		return 1;
	own.fps         = 60.0;
	own.beforeFrame = [ & ]( int ) { own.plugin.SetBeatInfo( 100.0f, 0.25f ); };
	own.Set( PT_MOTOR, static_cast< float >( Motor::Sync ) );
	own.Set( PT_STEP_ON, static_cast< float >( StepOn::Beat ) );
	own.Set( PT_STEP_ANGLE, ParamFromStepAngle( step ) );
	own.Set( PT_GLIDE, 0.0f );
	own.Set( PT_SPEED, ParamFromSpeed( 1.0 ) );
	if( !own.Render( 181 ) )
		return 1;
	const auto& starts = own.plugin.MotorForTest().StepStarts();
	double ownOff      = 0.0;
	for( size_t i = 0; i < starts.size(); ++i )
		ownOff = worse( ownOff, std::fabs( starts[ i ] - ( i + 1 ) * 0.6 ) );
	const double between = std::fabs( at25[ 1 ] - at25[ 0 ] );
	//The tolerance is the host's: FFGL hands the bar phase over as a float,
	//so each end of the frame is off by up to half an ulp below 1 (2^-25) of
	//a bar, and the beat found between them by up to 2^-24 bars. At the
	//glide's fastest (S pi / 2T turns a second) that is the phase's bound;
	//two rates, twice that.
	const double tolT   = std::ldexp( 1.0, -24 ) * 240.0 / bpm;
	const double tolPsi = 2.0 * tolT * S * kPi / ( 2.0 * T );
	Check( worst <= tolT && between <= tolPsi && starts.size() == 5 && ownOff <= 1e-9,
	       fmt( "128 BPM, Beat, 15 deg, Glide 0.5:%s (a float bar phase's bound %.1e s); psi at 2.5 s differs between the rates by %.1e turns (%.1e); own "
	            "clock at the last BPM (100): %zu steps in 3 s, off 0.6 s multiples by %.1e s (1e-9)",
	            detail.c_str(), tolT, between, tolPsi, starts.size(), ownOff ) );
	return Verdict();
}

//===========================================================================
// --cap: the gather never visits more than ( 2 K_max + 1 )^2 facets a pixel.
//===========================================================================
int runCap( const Perturb& perturb )
{
	std::printf( "\n=== cap: at the largest ball and the widest window, no pixel visits more than %d facets; at the defaults the cap never binds\n",
	             ( 2 * shaders::kMaxWindow + 1 ) * ( 2 * shaders::kMaxWindow + 1 ) );
	const int cap = ( 2 * shaders::kMaxWindow + 1 ) * ( 2 * shaders::kMaxWindow + 1 );
	for( const Raster& raster : kRasters )
	{
		auto worst = [ & ]( Rig& rig, int kMax, double& sum ) {
			TestRig t;
			t.kMax = kMax;
			rig.plugin.SetRigForTest( t );
			rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookRaw | shaders::kHookSpotsOnly | shaders::kHookCount );
			rig.Render( 1 );
			const Floats px = rig.Output();
			double most     = 0.0;
			sum             = 0.0;
			for( size_t i = 0; i < px.size(); i += 4 )
			{
				most = std::max( most, static_cast< double >( px[ i + 3 ] ) );
				sum += px[ i + 1 ];
			}
			return most;
		};
		Rig big;
		if( !startRig( big, raster.w, raster.h, perturb ) )
			return 1;
		big.Set( PT_DIAMETER, ParamFromDiameter( 1.0 ) );
		big.Set( PT_FACET_SIZE, ParamFromFacet( 0.005 ) );
		big.Set( PT_BEAM, ParamFromBeam( 6.0 ) );
		big.Set( PT_THROW, ParamFromThrow( 0.9 ) );
		big.Set( PT_VIEW, static_cast< float >( View::Room ) );
		double capped = 0.0, uncapped = 0.0;
		const double mostBig = worst( big, perturb.kMax, capped );
		const double mostAll = worst( big, 24, uncapped );
		Rig defaults;
		if( !startRig( defaults, raster.w, raster.h, perturb ) )
			return 1;
		double ignored       = 0.0;
		const double mostDef = worst( defaults, perturb.kMax, ignored );
		Check( mostBig <= cap && mostBig > 0.0 && mostDef < cap,
		       fmt( "%dx%d: 1 m ball, 5 mm facets (%d), beam 6 deg at 0.9 m, the room: at most %.0f candidates a pixel (cap %d); without the cap %.0f, and the "
		            "cap loses %.2f%% of the light on the room; at the defaults at most %.0f",
		            raster.w, raster.h, big.plugin.LastFrame().layout.total, mostBig, cap, mostAll, 100.0 * ( 1.0 - capped / uncapped ), mostDef ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha: the Over's output alpha, decided, on a clip with alpha.
//===========================================================================
/// The harness's card with alpha 0, 0.5 and 1 in diagonal stripes of
/// squares an eighth of the frame high, so every spot's window of the gobo
/// (about a seventh of it) and every part of the wall sees all three; `under`
/// painted under alpha 0.
Floats alphaCard( int w, int h, float under )
{
	Floats card = buildCard( w, h );
	const int side = std::max( h / 8, 1 );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			float* o        = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
			const int level = ( x / side + y / side ) % 3;
			o[ 3 ]          = level == 0 ? 0.0f : level == 1 ? 0.5f : 1.0f;
			if( level == 0 )
				o[ 0 ] = o[ 1 ] = o[ 2 ] = under;
		}
	return card;
}

int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: Mix 0 returns the clip; Surface keeps the clip's alpha and shows nothing under a transparent pixel; Gobo's alpha is mix(a, 1, Mix); "
	             "the source is opaque\n" );
	const Raster raster = kRasters.back();
	const Floats black  = alphaCard( raster.w, raster.h, 0.0f ), white = alphaCard( raster.w, raster.h, 1.0f );
	auto shoot = [ & ]( const Floats& card, ClipAs as, float mix, int hooks, Floats& out ) {
		Rig rig( true );
		if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
			return false;
		rig.plugin.SetHooksForTest( perturb.hooks | hooks );
		rig.Set( PT_CLIP_AS, static_cast< float >( as ) );
		rig.Set( PT_MIX, mix );
		if( !rig.Render( 3 ) )
			return false;
		out = rig.Output();
		return true;
	};
	Floats out, out2;
	//Mix 0, both modes: the clip, exactly.
	int differ0 = 0;
	for( ClipAs as : { ClipAs::Surface, ClipAs::Gobo } )
	{
		if( !shoot( white, as, 0.0f, 0, out ) )
			return 1;
		for( size_t i = 0; i < out.size(); ++i )
			differ0 += out[ i ] != white[ i ];
	}
	//Surface at Mix 1 and 0.5: alpha is the clip's; under alpha 0 nothing
	//shows, and black or white under it makes no difference.
	int alphaOff = 0, underDiffer = 0;
	for( float mix : { 1.0f, 0.5f } )
	{
		if( !shoot( black, ClipAs::Surface, mix, 0, out ) || !shoot( white, ClipAs::Surface, mix, 0, out2 ) )
			return 1;
		for( size_t i = 0; i < out.size(); i += 4 )
		{
			alphaOff += out[ i + 3 ] != black[ i + 3 ];
			for( int c = 0; c < 3; ++c )
				underDiffer += out[ i + c ] != out2[ i + c ];
		}
	}
	//Gobo at Mix 1 and 0.5.
	double gAlpha = 0.0;
	int gUnder    = 0;
	for( float mix : { 1.0f, 0.5f } )
	{
		if( !shoot( black, ClipAs::Gobo, mix, 0, out ) || !shoot( white, ClipAs::Gobo, mix, 0, out2 ) )
			return 1;
		for( size_t i = 0; i < out.size(); i += 4 )
		{
			const double a = black[ i + 3 ];
			gAlpha         = worse( gAlpha, std::fabs( out[ i + 3 ] - ( a + ( 1.0 - a ) * mix ) ) );
			for( int c = 0; c < 3; ++c )
				gUnder += out[ i + c ] != out2[ i + c ];
		}
	}
	//Surface, Room Light 1 and nothing lit: the clip back through sRGB both
	//ways (pow is exp2( y log2 x ), 3 ULP each: 1e-4 in code is generous).
	double roundTrip = 0.0;
	{
		Rig rig( true );
		Floats opaque = buildCard( raster.w, raster.h );
		if( !startRig( rig, raster.w, raster.h, perturb, &opaque ) )
			return 1;
		TestRig t;
		t.soloRing  = -2;
		t.soloIndex = -2;
		t.kMax      = perturb.kMax;
		rig.plugin.SetRigForTest( t );
		rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookSpotsOnly );
		rig.Set( PT_ROOM_LIGHT, 1.0f );
		rig.Set( PT_CLIP_AS, static_cast< float >( ClipAs::Surface ) );
		rig.Set( PT_MIX, 1.0f );
		if( !rig.Render( 1 ) )
			return 1;
		const Floats o = rig.Output();
		for( size_t i = 0; i < o.size(); ++i )
			roundTrip = worse( roundTrip, std::fabs( o[ i ] - opaque[ i ] ) );
	}
	//The source: opaque everywhere.
	double sourceAlpha = 1.0;
	{
		Rig rig( false );
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		if( !rig.Render( 3 ) )
			return 1;
		const Floats o = rig.Output();
		for( size_t i = 3; i < o.size(); i += 4 )
			sourceAlpha = std::min( sourceAlpha, static_cast< double >( o[ i ] ) );
	}
	Check( differ0 == 0, fmt( "Mix 0, Surface and Gobo: %d values differ from the clip (texelFetch: 0)", differ0 ) );
	Check( alphaOff == 0 && underDiffer == 0, fmt( "Surface, Mix 1 and 0.5: alpha off the clip's at %d pixels; white under alpha 0 changes %d values (0)", alphaOff, underDiffer ) );
	Check( gAlpha <= 1e-6 && gUnder == 0, fmt( "Gobo, Mix 1 and 0.5: alpha off mix(a, 1, Mix) by %.1e (1e-6); white under alpha 0 changes %d values (0)", gAlpha, gUnder ) );
	Check( roundTrip <= 1e-4, fmt( "Surface, Room Light 1, nothing lit: the clip back to %.1e (1e-4)", roundTrip ) );
	Check( sourceAlpha == 1.0, fmt( "the source's alpha: %.6f everywhere (1)", sourceAlpha ) );
	return Verdict();
}

//===========================================================================
// --resize and --state.
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: after the host's raster changes (and its shape), the picture is exactly a fresh instance's that saw the same frames\n" );
	const Raster small = kRasters.back(), big = { 2 * small.w, 2 * small.h }, tall = { small.w, small.w * 3 / 4 };
	for( const Raster& to : { small, tall } )
	{
		Rig resized;
		if( !startRig( resized, big.w, big.h, perturb ) )
			return 1;
		resized.Set( PT_SPEED, ParamFromSpeed( 3.0 ) );
		if( !resized.Render( 70 ) || !resized.Resize( to.w, to.h ) || !resized.Render( 3 ) )
			return 1;
		Rig fresh;
		if( !startRig( fresh, to.w, to.h, Perturb {} ) )
			return 1;
		fresh.Set( PT_SPEED, ParamFromSpeed( 3.0 ) );
		if( !fresh.Render( 73 ) )
			return 1;
		const Floats x = resized.Output(), y = fresh.Output();
		int differ = 0;
		double lit = 0.0;
		for( size_t i = 0; i < x.size() && i < y.size(); ++i )
		{
			differ += byteOf( x[ i ] ) != byteOf( y[ i ] );
			lit = std::max( lit, static_cast< double >( y[ i ] ) );
		}
		const double psi = fresh.plugin.LastFrame().psi;
		Check( differ == 0 && lit > 0.1 && psi > 0.0,
		       fmt( "%dx%d for 70 frames, then %dx%d for 3, against %dx%d for all 73: %d bytes differ (the ball turned %.4f turns)", big.w, big.h, to.w, to.h, to.w,
		            to.h, differ, psi ) );
	}
	return Verdict();
}

int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back, both plugins\n" );
	for( bool over : { false, true } )
	{
		Rig rig( over );
		if( !rig.Init( 320, 180 ) )
			return 1;
		GLuint hostArray = 0, hostBuffer = 0;
		glGenVertexArrays( 1, &hostArray );
		glGenBuffers( 1, &hostBuffer );
		int problems = 0;
		std::string what;
		for( int frame = 0; frame < 6; ++frame )
		{
			rig.Set( PT_GOBO, static_cast< float >( frame % 5 ) );
			rig.Set( PT_DIAMETER, frame < 3 ? 0.3f : 0.6f );
			if( over )
				rig.Set( PT_CLIP_AS, static_cast< float >( frame % 2 ) );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 0, 0, 320, 180 );
			glBindVertexArray( hostArray );
			glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
			glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
			glEnable( GL_SCISSOR_TEST );
			glScissor( 0, 0, 320, 180 );
			glActiveTexture( GL_TEXTURE0 );
			glUseProgram( 0 );
			rig.plugin.SetTime( frame / 60.0 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0;
			GLfloat clear[ 4 ]   = {};
			GLboolean mask[ 4 ]  = {};
			glGetIntegerv( GL_VIEWPORT, viewport );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
			glGetIntegerv( GL_CURRENT_PROGRAM, &program );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
			glGetBooleanv( GL_COLOR_WRITEMASK, mask );
			auto expect = [ & ]( bool ok, const char* name ) {
				if( !ok )
				{
					++problems;
					what += std::string( " " ) + name;
				}
			};
			expect( viewport[ 0 ] == 0 && viewport[ 1 ] == 0 && viewport[ 2 ] == 320 && viewport[ 3 ] == 180, "viewport" );
			expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
			expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
			expect( program == 0, "program" );
			expect( unit == GL_TEXTURE0, "active-unit" );
			expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
			expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
			expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
			expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
			expect( mask[ 0 ] && mask[ 1 ] && mask[ 2 ] && mask[ 3 ], "colour-mask" );
			for( int u = 0; u < 8; ++u )
			{
				GLint bound = 0;
				glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
				glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
				expect( bound == 0, "texture-unit" );
			}
			glActiveTexture( GL_TEXTURE0 );
		}
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glDeleteVertexArrays( 1, &hostArray );
		glDeleteBuffers( 1, &hostBuffer );
		Check( problems == 0, fmt( "%s, six frames, every gobo, a new ball: viewport, vertex array, array buffer, program, active unit, framebuffer, "
		                           "blend, scissor, clear colour, colour mask, eight texture units (%d wrong:%s)",
		                           over ? "SW Mirror Over" : "SW Mirrorball", problems, what.empty() ? " none" : what.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --names, --cues, --layout (no GL).
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too), within FFGL's 16 characters, the About block last\n" );
	for( bool over : { false, true } )
	{
		MirrorballPlugin plugin( over );
		std::map< std::string, int > seen, address;
		int longNames = 0, dupes = 0, clashes = 0, slashes = 0;
		for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
		{
			const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
			if( name.size() > 16 )
			{
				std::printf( "    too long: %s\n", name.c_str() );
				++longNames;
			}
			if( seen[ name ]++ > 0 )
				++dupes;
			slashes += name.find( '/' ) != std::string::npos;
			std::string key;
			for( char c : name )
				if( c != ' ' )
					key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			if( address[ key ]++ > 0 )
				++clashes;
		}
		const unsigned int aboutFirst = plugin.GetNumParams() - stoatworks::about::kParamCount;
		const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
		Check( longNames == 0 && dupes == 0 && clashes == 0 && slashes == 0 && aboutLast,
		       fmt( "%s: %u parameters, %d too long, %d duplicated, %d clashing addresses, %d with a '/'; the About block is last (%s)",
		            over ? "SW Mirror Over" : "SW Mirrorball", plugin.GetNumParams(), longNames, dupes, clashes, slashes, aboutLast ? "yes" : "NO" ) );
	}
	return Verdict();
}

int runCues( const Perturb& perturb )
{
	std::printf( "\n=== cues: a cue sheet steps options, ramps standard controls, and names an option by its name\n" );
	MirrorballPlugin plugin( false );
	std::istringstream sheet( "0 Motor Free\n60 Motor Sync\n0 Step On Beat\n60 Step On 2 Beats\n0 Speed 0\n60 Speed 1\n" );
	std::map< unsigned int, Cue > cues;
	std::string error;
	bindScript( plugin, sheet, "cues", cues, error );
	int bad = 0;
	std::string what;
	for( const auto& entry : cues )
	{
		const unsigned int type = plugin.GetParamType( entry.first );
		const bool ramp         = perturb.cuesRamp ? true : entry.second.ramp;
		const float mid         = valueAt( entry.second.track, 29, ramp );
		const float a = entry.second.track.front().second, b = entry.second.track.back().second;
		const bool expectRamp = type == FF_TYPE_STANDARD;
		const bool ok         = expectRamp ? ( mid > a && mid < b ) : ( mid == a && valueAt( entry.second.track, 60, ramp ) == b );
		bad += !ok;
		what += fmt( " %s %s(%g at frame 29)", plugin.GetParamName( entry.first ), expectRamp ? "ramps " : "steps ", mid );
	}
	const auto motor  = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_MOTOR ) ) );
	const auto stepOn = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_STEP_ON ) ) );
	const bool named  = motor != cues.end() && motor->second.track.back().second == static_cast< float >( Motor::Sync ) && stepOn != cues.end()
	                   && stepOn->second.track.back().second == static_cast< float >( StepOn::TwoBeats );
	Check( error.empty() && bad == 0 && cues.size() == 3 && named,
	       fmt( "%d wrong:%s; 'Sync' and '2 Beats' read as their options: %s", bad, what.c_str(), named ? "yes" : "NO" ) );
	return Verdict();
}

int runLayout( const Perturb& )
{
	std::printf( "\n=== layout: the facet counts SPEC-mirrorball.md gives, no two facets overlapping, the area estimate, and the missing hash's count\n" );
	struct Row
	{
		double d, s;
		int total, rings, equator;
	};
	const Row rows[] = { { 0.2, 0.005, 4128, -1, -1 }, { 0.2, 0.01, 1025, -1, -1 },  { 0.2, 0.02, 250, -1, -1 },    { 0.5, 0.005, 25887, 141, 285 },
		                 { 0.5, 0.01, 6452, 70, 142 }, { 0.5, 0.02, 1602, -1, -1 }, { 1.0, 0.005, 103702, 284, 571 }, { 1.0, 0.01, 25887, -1, -1 },
		                 { 1.0, 0.02, 6452, -1, -1 } };
	int wrong = 0, overlaps = 0;
	double worstArea = 0.0;
	for( const Row& row : rows )
	{
		const ball::Layout l = ball::Build( row.d, row.s );
		int equator          = 0;
		for( const ball::Ring& r : l.rings )
		{
			equator = std::max( equator, r.count );
			//Neighbours in a ring, and the next ring, a chord apart: never
			//closer than a facet's side.
			if( r.count > 1 && 2.0 * l.radius * r.cosPhi * std::sin( kPi / r.count ) < l.facet )
				++overlaps;
		}
		if( 2.0 * l.radius * std::sin( 0.5 * l.delta ) < l.facet )
			++overlaps;
		wrong += l.total != row.total || ( row.rings > 0 && l.Rings() != row.rings ) || ( row.equator > 0 && equator != row.equator );
		worstArea = std::max( worstArea, std::fabs( l.total / ( 4.0 * kPi * l.radius * l.radius / ( l.pitch * l.pitch ) ) - 1.0 ) );
	}
	//The hash: Missing 30% on the 1 m / 5 mm ball, against the binomial count.
	const ball::Layout big = ball::Build( 1.0, 0.005 );
	const uint32_t below   = ball::MissingThreshold( 0.3 );
	long missing           = 0;
	for( int k = 0; k < big.Rings(); ++k )
		for( int j = 0; j < big.rings[ static_cast< size_t >( k ) ].count; ++j )
			missing += ball::IsMissing( k, j, 1, below );
	const double expect = 0.3 * big.total, sd = std::sqrt( big.total * 0.3 * 0.7 );
	Check( wrong == 0 && overlaps == 0 && worstArea < 0.04 && std::fabs( missing - expect ) <= 3.0 * sd,
	       fmt( "nine balls: %d counts wrong, %d overlaps; within %.2f%% of 4 pi R^2 / p^2; Missing 30%% loses %ld of %d (expected %.0f +- %.0f, 3 sd)", wrong, overlaps,
	            100.0 * worstArea, missing, big.total, expect, 3.0 * sd ) );
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "reflect", runReflect, false }, { "area", runArea, false },   { "flux", runFlux, false },     { "circles", runCircles, false },
		{ "window", runWindow, false },   { "missing", runMissing, false }, { "rate", runRate, false }, { "clock", runClock, false },
		{ "sync", runSync, false },       { "cap", runCap, false },     { "alpha", runAlpha, false },   { "resize", runResize, false },
		{ "state", runState, false },     { "names", runNames, true },  { "cues", runCues, true },      { "layout", runLayout, true },
	};
	return list;
}

bool isOffline( const char* name )
{
	for( const CheckEntry& c : checks() )
		if( std::strcmp( c.flag, name ) == 0 )
			return c.offline;
	return false;
}

int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	using namespace shaders;
	add( "reflect", runReflect, "the spot follows the facet's normal", []( Perturb& p ) { p.hooks = kHookNormalSpot; } );
	add( "reflect", runReflect, "reflection without its factor 2", []( Perturb& p ) { p.hooks = kHookNoFactor2; } );
	add( "area", runArea, "the gate's cone from the lamp's mirror image", []( Perturb& p ) { p.hooks = kHookVirtualApex; } );
	add( "area", runArea, "the pupil ignored: every spot the gate's full cone", []( Perturb& p ) { p.hooks = kHookNoWindow; } );
	add( "flux", runFlux, "the facet's solid angle without its cosine", []( Perturb& p ) { p.hooks = kHookNoFacetCos; } );
	add( "flux", runFlux, "the facet's solid angle without 1/D^2", []( Perturb& p ) { p.hooks = kHookNoInvSquare; } );
	add( "circles", runCircles, "each ring turning at omega cos(latitude)", []( Perturb& p ) { p.hooks = kHookDifferential; } );
	add( "window", runWindow, "the pupil ignored: every spot the whole gobo", []( Perturb& p ) { p.hooks = kHookNoWindow; } );
	add( "window", runWindow, "the reflected gobo read unmirrored", []( Perturb& p ) { p.hooks = kHookUnmirrored; } );
	add( "missing", runMissing, "the missing facets' hash salted by the frame", []( Perturb& p ) { p.hooks = kHookSaltByFrame; } );
	add( "rate", runRate, "Free advancing 1/60 s a frame whatever the frame's length", []( Perturb& p ) { p.motorHooks = kMotorPerFrame; } );
	add( "clock", runClock, "the phase from the host's absolute clock in float", []( Perturb& p ) { p.motorHooks = kMotorFloatClock; } );
	add( "sync", runSync, "a step at its frame, not where the beat fell inside it", []( Perturb& p ) { p.motorHooks = kMotorStepAtFrame; } );
	add( "cap", runCap, "the cap lifted (K up to 24)", []( Perturb& p ) { p.kMax = 24; } );
	add( "alpha", runAlpha, "Gobo passes the clip's alpha through", []( Perturb& p ) { p.hooks = kHookAlphaThrough; } );
	add( "alpha", runAlpha, "Surface keeps the colour under alpha 0", []( Perturb& p ) { p.hooks = kHookAlphaUnmasked; } );
	add( "resize", runResize, "the motor reset when the frame changes size", []( Perturb& p ) { p.pluginHooks = kPluginResetOnResize; } );
	add( "cues", runCues, "ramp every control between keys", []( Perturb& p ) { p.cuesRamp = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 320x180 and above, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" } };
	struct Case
	{
		const char* what;
		bool over;
		std::function< void( Rig& ) > set;
	};
	auto worst = []( Rig& r ) {
		r.Set( PT_DIAMETER, ParamFromDiameter( 1.0 ) );
		r.Set( PT_FACET_SIZE, ParamFromFacet( 0.005 ) );
		r.Set( PT_BEAM, ParamFromBeam( 6.0 ) );
		r.Set( PT_THROW, ParamFromThrow( 0.9 ) );
	};
	const Case cases[] = {
		{ "source, defaults (Room)", false, []( Rig& ) {} },
		{ "source, Wall", false, []( Rig& r ) { r.Set( PT_VIEW, static_cast< float >( View::Wall ) ); } },
		{ "source, 103,702 facets, widest window", false, worst },
		{ "Over, defaults (Surface)", true, []( Rig& ) {} },
		{ "Over, Gobo", true, []( Rig& r ) { r.Set( PT_CLIP_AS, static_cast< float >( ClipAs::Gobo ) ); } },
		{ "Over, 103,702 facets, widest window", true, worst },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED), after 240 frames of warm-up, median and worst of 120 (no 4K: a shared machine)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			Rig rig( c.over );
			rig.bpm = 120.0;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			c.set( rig );
			if( !rig.Render( 240 ) )
				return 1;
			const int kTimed = 120;
			std::vector< double > times, cpu;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				times.push_back( static_cast< double >( ns ) * 1e-6 );
				cpu.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			std::sort( times.begin(), times.end() );
			std::sort( cpu.begin(), cpu.end() );
			std::printf( "  %-38s %-6s GPU median %6.2f ms, worst %6.2f  (%5.1f%% of a 60 fps frame); wall %5.2f ms\n", c.what, size.name, times[ kTimed / 2 ],
			             times.back(), 100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ), cpu[ kTimed / 2 ] );
		}
	glDeleteQueries( 1, &query );
	return 0;
}
