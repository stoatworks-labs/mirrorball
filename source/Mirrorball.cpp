#include "Mirrorball.h"

#include "Diag.h"
#include "GLState.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace mirrorball
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Host seconds a single frame may advance by: a stalled host must not
/// throw the ball a tenth of a second of motion at once.
constexpr double kMaxFrameDelta = 0.1;
/// The lamp stays this far off the ball's surface.
constexpr double kLampClearance = 0.15;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

/// Every pass here writes every texel of its target, so what the target held
/// does not matter -- but Apple's GL runs on Metal, where binding a
/// framebuffer LOADS the attachment unless it is cleared first. A clear
/// becomes the pass's load action, which costs nothing.
void discardTarget()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
MirrorballPlugin::MirrorballPlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );

	//The motor turns in revolutions a minute and steps on the beat, so the
	//plugin has to know what a second is.
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		if( id >= PT_ABOUT_TEXT )
			break;
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( host, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Integer:
			SetParamInfo( host, info.name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( host, info.low, info.high );
			break;
		case Kind::Option:
			SetOptionParamInfo( host, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		}
		SetParamGroup( host, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	const unsigned int aboutHost = static_cast< unsigned int >( idToHost[ PT_ABOUT_TEXT ] );
	SetParamInfo( aboutHost, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = aboutHost + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = aboutHost; i < hostOrder.size(); ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint MirrorballPlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}
void MirrorballPlugin::uniform( const FFGLShader& shader, const char* name, int v )
{
	glUniform1i( where( shader, name ), v );
}
void MirrorballPlugin::uniform( const FFGLShader& shader, const char* name, float v )
{
	glUniform1f( where( shader, name ), v );
}
void MirrorballPlugin::uniform( const FFGLShader& shader, const char* name, float a, float b )
{
	glUniform2f( where( shader, name ), a, b );
}
void MirrorballPlugin::uniform( const FFGLShader& shader, const char* name, const Vec3& v )
{
	glUniform3f( where( shader, name ), static_cast< float >( v[ 0 ] ), static_cast< float >( v[ 1 ] ), static_cast< float >( v[ 2 ] ) );
}

//---------------------------------------------------------------------------
FFResult MirrorballPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) + ( isEffect ? " (Over)" : " (source)" ) );

	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &goboShader, shaders::Pass::Gobo, "gobo" },
		{ &finalShader, shaders::Pass::Final, "final" },
	};
	for( const Stage& stage : stages )
	{
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Mirrorball: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
void MirrorballPlugin::drawGobo( const FFGLTextureStruct* input, int kind )
{
	{
		ScopedFBOBinding target( gobo.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glViewport( 0, 0, shaders::kGoboSize, shaders::kGoboSize );
		discardTarget();
		glUseProgram( goboShader.GetGLID() );
		//Always a real texture on the clip's unit, even in the source: a
		//sampler on texture 0 is "unloadable" to Apple's GL (boreal's trap).
		bindUnit( 0, input != nullptr ? input->Handle : ringTable.TextureID() );
		uniform( goboShader, "InputTexture", 0 );
		if( input != nullptr )
		{
			const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *input );
			uniform( goboShader, "MaxUV", maxCoords.s, maxCoords.t );
			uniform( goboShader, "ClipAspect", static_cast< float >( input->Width ) / static_cast< float >( std::max( input->Height, 1u ) ) );
		}
		else
		{
			uniform( goboShader, "MaxUV", 1.0f, 1.0f );
			uniform( goboShader, "ClipAspect", 1.0f );
		}
		uniform( goboShader, "GoboKind", kind );
		uniform( goboShader, "Hooks", hooks );
		quad.Draw();
		unbindTextureUnits( 1 );
		glUseProgram( 0 );
	}
	gobo.GenerateMipmaps();
	goboBuilt = kind;
}

void MirrorballPlugin::uploadRings()
{
	std::vector< float > texels( static_cast< size_t >( ball::kMaxRings ) * 4, 0.0f );
	for( int k = 0; k < frame.layout.Rings(); ++k )
	{
		const ball::Ring& r                     = frame.layout.rings[ static_cast< size_t >( k ) ];
		texels[ static_cast< size_t >( k ) * 4 + 0 ] = static_cast< float >( r.sinPhi );
		texels[ static_cast< size_t >( k ) * 4 + 1 ] = static_cast< float >( r.cosPhi );
		texels[ static_cast< size_t >( k ) * 4 + 2 ] = static_cast< float >( r.count );
		texels[ static_cast< size_t >( k ) * 4 + 3 ] = static_cast< float >( r.offset );
	}
	bindUnit( 0, ringTable.TextureID() );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, ball::kMaxRings, 1, GL_RGBA, GL_FLOAT, texels.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

void MirrorballPlugin::drawFinal( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( finalShader.GetGLID() );
	bindUnit( 0, ringTable.TextureID() );
	bindUnit( 1, gobo.TextureID() );
	bindUnit( 2, input != nullptr ? input->Handle : ringTable.TextureID() );
	uniform( finalShader, "RingTable", 0 );
	uniform( finalShader, "Gobo", 1 );
	uniform( finalShader, "InputTexture", 2 );

	const room::View& v = frame.view;
	const double aspect = static_cast< double >( width ) / std::max( height, 1 );
	uniform( finalShader, "ViewMode", v.ortho ? 1 : 0 );
	uniform( finalShader, "Eye", v.eye );
	uniform( finalShader, "Forward", v.forward );
	uniform( finalShader, "Right", v.right );
	uniform( finalShader, "Up", v.up );
	uniform( finalShader, "TanHalf", static_cast< float >( v.tanHalfV * aspect ), static_cast< float >( v.tanHalfV ) );
	uniform( finalShader, "PlaneCentre", v.centre );
	uniform( finalShader, "PlaneRight", v.right );
	uniform( finalShader, "PlaneUp", v.up );
	uniform( finalShader, "PlaneNormal", v.normal );
	uniform( finalShader, "PlaneHalf", static_cast< float >( v.halfHeight * aspect ), static_cast< float >( v.halfHeight ) );
	uniform( finalShader, "BoxMin", frame.room.min );
	uniform( finalShader, "BoxMax", frame.room.max );
	uniform( finalShader, "WallAlbedo", static_cast< float >( room::kWallAlbedo ) );
	uniform( finalShader, "CeilingAlbedo", static_cast< float >( room::kCeilingAlbedo ) );
	uniform( finalShader, "FloorAlbedo", static_cast< float >( room::kFloorAlbedo ) );

	const ball::Layout& b = frame.layout;
	uniform( finalShader, "BallRadius", static_cast< float >( b.radius ) );
	uniform( finalShader, "FacetSize", static_cast< float >( b.facet ) );
	uniform( finalShader, "Delta", static_cast< float >( b.delta ) );
	uniform( finalShader, "Rings", b.Rings() );
	uniform( finalShader, "Psi", static_cast< float >( frame.psi ) );
	glUniform1ui( where( finalShader, "MissingBelow" ), frame.missingBelow );
	uniform( finalShader, "Seed", frame.seed );
	uniform( finalShader, "FrameSalt", frame.frame );
	glUniform2i( where( finalShader, "Solo" ), testRig.soloRing, testRig.soloIndex );
	uniform( finalShader, "KMax", frame.kMax );

	const lamp::Lamp& l = frame.lamp;
	double rgb[ 3 ];
	ColourOf( static_cast< Colour >( OptionIndex( params[ PT_COLOUR ], static_cast< int >( Colour::Count ) ) ), rgb );
	uniform( finalShader, "LampPos", l.position );
	uniform( finalShader, "LampAxis", l.axis );
	uniform( finalShader, "LampRight", l.right );
	uniform( finalShader, "LampUp", l.up );
	uniform( finalShader, "TanAlpha", static_cast< float >( l.tanAlpha ) );
	uniform( finalShader, "Alpha", static_cast< float >( l.alpha ) );
	uniform( finalShader, "PupilRadius", static_cast< float >( 0.5 * lamp::kPupilDiameter ) );
	uniform( finalShader, "Luminance", Vec3 { ball::kReflectance * l.luminance * rgb[ 0 ], ball::kReflectance * l.luminance * rgb[ 1 ],
	                                          ball::kReflectance * l.luminance * rgb[ 2 ] } );
	uniform( finalShader, "Intensity", Vec3 { l.intensity * rgb[ 0 ], l.intensity * rgb[ 1 ], l.intensity * rgb[ 2 ] } );
	uniform( finalShader, "Reflectance", static_cast< float >( ball::kReflectance ) );

	uniform( finalShader, "Gain", static_cast< float >( frame.gain ) );
	uniform( finalShader, "RoomLight", static_cast< float >( frame.roomLight ) );
	uniform( finalShader, "IsOver", isEffect ? 1 : 0 );
	uniform( finalShader, "ClipAs", isEffect ? OptionIndex( params[ PT_CLIP_AS ], static_cast< int >( ClipAs::Count ) ) : 0 );
	uniform( finalShader, "MixAmount", isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
	glUniform4i( where( finalShader, "Viewport" ), hostViewport[ 0 ], hostViewport[ 1 ], width, height );
	uniform( finalShader, "Hooks", hooks );
	quad.Draw();
	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void MirrorballPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (rosette's, by way of haze).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

//---------------------------------------------------------------------------
FFResult MirrorballPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = input != nullptr ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height = input != nullptr ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;
	if( ( pluginHooks & kPluginResetOnResize ) && lastWidth > 0 && ( width != lastWidth || height != lastHeight ) )
		motor.Reset();
	lastWidth  = width;
	lastHeight = height;

	//---------------------------------------------------------------------
	// Time and the beat; the motor.
	//---------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f )
		hostBeatSeen = true;
	lastBarPhase = barPhase;

	MotorInputs in;
	in.mode      = static_cast< Motor >( OptionIndex( params[ PT_MOTOR ], static_cast< int >( Motor::Count ) ) );
	in.rpm       = SpeedFromParam( params[ PT_SPEED ] );
	in.stepOn    = static_cast< StepOn >( OptionIndex( params[ PT_STEP_ON ], static_cast< int >( StepOn::Count ) ) );
	in.stepTurns = StepAngleFromParam( params[ PT_STEP_ANGLE ] ) / 360.0;
	in.glide     = GlideFromParam( params[ PT_GLIDE ] );
	in.bpm       = bpm;
	in.barPhase  = barPhase;
	in.hostBeat  = hostBeatSeen;
	motor.Advance( now, dt, in );

	//---------------------------------------------------------------------
	// The ball, the lamp, the room, the view.
	//---------------------------------------------------------------------
	const double diameter = DiameterFromParam( params[ PT_DIAMETER ] );
	const double facet    = FacetFromParam( params[ PT_FACET_SIZE ] );
	if( diameter != ringsDiameter || facet != ringsFacet )
		frame.layout = ball::Build( diameter, facet );

	frame.room = room::Of( RoomFromParam( params[ PT_ROOM ] ) );
	if( testRig.lamp )
		frame.lamp = lamp::At( testRig.lampPos, BeamFromParam( params[ PT_BEAM ] ) );
	else
		frame.lamp = lamp::Place( std::max( ThrowFromParam( params[ PT_THROW ] ), frame.layout.radius + kLampClearance ),
		                          ElevationFromParam( params[ PT_ELEVATION ] ), BearingFromParam( params[ PT_BEARING ] ),
		                          BeamFromParam( params[ PT_BEAM ] ), frame.room );

	const View view = isEffect ? View::Wall : static_cast< View >( OptionIndex( params[ PT_VIEW ], static_cast< int >( View::Count ) ) );
	if( testRig.plane )
		frame.view = room::Plane( testRig.centre, testRig.normal, testRig.upHint, testRig.halfHeight );
	else
		frame.view = view == View::Wall ? room::WallPlane( frame.room ) : room::RoomCamera( frame.room );

	frame.psi          = testRig.psi ? testRig.psiTurns - std::floor( testRig.psiTurns ) : motor.Phase();
	frame.gain         = std::pow( 2.0, ExposureFromParam( params[ PT_EXPOSURE ] ) ) / kReferenceLux;
	frame.roomLight    = RoomLightFromParam( params[ PT_ROOM_LIGHT ] );
	frame.width        = width;
	frame.height       = height;
	frame.kMax         = std::max( 0, testRig.kMax );
	frame.missingBelow = ball::MissingThreshold( MissingFromParam( params[ PT_MISSING ] ) );
	frame.seed         = static_cast< int >( std::lround( std::clamp( params[ PT_SEED ], 0.0f, 999.0f ) ) );
	++frame.frame;

	//---------------------------------------------------------------------
	// The buffers, allocated before anything is bound.
	//---------------------------------------------------------------------
	if( !gobo.Ensure( shaders::kGoboSize, shaders::kGoboSize, GL_RGBA16F, PassBuffer::Sampling::Mipmapped )
	    || !ringTable.Ensure( ball::kMaxRings, 1, GL_RGBA32F, PassBuffer::Sampling::Nearest ) )
	{
		diag::error( "could not allocate the buffers" );
		return FF_FAIL;
	}
	if( diameter != ringsDiameter || facet != ringsFacet )
	{
		uploadRings();
		ringsDiameter = diameter;
		ringsFacet    = facet;
	}

	int kind = OptionIndex( params[ PT_GOBO ], static_cast< int >( Gobo::Count ) );
	if( isEffect && static_cast< ClipAs >( OptionIndex( params[ PT_CLIP_AS ], static_cast< int >( ClipAs::Count ) ) ) == ClipAs::Gobo )
		kind = static_cast< int >( Gobo::Clip );
	if( kind == static_cast< int >( Gobo::Clip ) || kind != goboBuilt )
		drawGobo( kind == static_cast< int >( Gobo::Clip ) ? input : nullptr, kind );
	drawFinal( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult MirrorballPlugin::DeInitGL()
{
	uniformCache.clear();
	goboShader.FreeGLResources();
	finalShader.FreeGLResources();
	quad.Release();
	gobo.Destroy();
	ringTable.Destroy();
	goboBuilt     = -1;
	ringsDiameter = -1.0;
	ringsFacet    = -1.0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult MirrorballPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void MirrorballPlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

char* MirrorballPlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult MirrorballPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

void MirrorballPlugin::SetById( unsigned int id, float value )
{
	if( id >= PT_COUNT || idToHost[ id ] < 0 )
		return;
	SetFloatParameter( static_cast< unsigned int >( idToHost[ id ] ), value );
}

FFResult MirrorballPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];

	// The About buttons open a browser and store nothing.
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;
	params[ id ] = value;
	return FF_SUCCESS;
}

float MirrorballPlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace mirrorball
