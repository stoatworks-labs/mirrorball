/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call. (fountain's glue, with
    mirrorball's names; mirrorball has no Audio buffer, so none of fountain's
    harness music is here.)

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Mirrorball.cpp` and everything it calls, compiled
    UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `MirrorballPlugin`, as SourcePlugin.cpp /
        EffectPlugin.cpp's registrations would, and calls InitGL / DeInitGL
        on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTime, SetBeatInfo
        and ProcessOpenGL with a viewport (the source) or one input texture
        (the Over), as `tools/mbtest`'s rig does.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Mirrorball.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace mirrorball;

namespace
{
/// One plugin instance and the host-side structs it is handed.
struct Instance
{
	explicit Instance( bool effect ) : plugin( effect )
	{
	}

	MirrorballPlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

/// The ParamId behind a host index of this instance, or PT_COUNT.
unsigned int idOf( Instance* instance, int index )
{
	for( unsigned int candidate = 0; candidate < PT_ABOUT_TEXT; ++candidate )
		if( instance->plugin.HostIndexOf( candidate ) == index )
			return candidate;
	return PT_COUNT;
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* mb_new( int effect )
{
	return new Instance( effect != 0 );
}

EMSCRIPTEN_KEEPALIVE void mb_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int mb_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int mb_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* mb_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int mb_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* mb_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it; FF_TYPE_INTEGER the
/// count itself).
EMSCRIPTEN_KEEPALIVE float mb_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int mb_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* mb_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float mb_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

/// An FF_TYPE_INTEGER's declared range (SetParamRange).
EMSCRIPTEN_KEEPALIVE float mb_param_range_min( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).min;
}

EMSCRIPTEN_KEEPALIVE float mb_param_range_max( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).max;
}

EMSCRIPTEN_KEEPALIVE int mb_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int mb_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int mb_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float mb_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// Resolume's SetBeatInfo, sent every frame: the tempo and the bar's phase.
EMSCRIPTEN_KEEPALIVE void mb_set_beat( Instance* instance, float bpm, float barPhase )
{
	instance->plugin.SetBeatInfo( bpm, barPhase );
}

/// One frame: SetTime, then ProcessOpenGL into whatever the page has bound.
/// The source reads its size from the viewport the page set; the Over is
/// handed one input of clipWidth x clipHeight whose GL name is `clip` (a
/// texture the page registered with emscripten's GL tables). 1 on success.
EMSCRIPTEN_KEEPALIVE int mb_process( Instance* instance, double seconds, int clip, int clipWidth, int clipHeight )
{
	instance->plugin.SetTime( seconds );
	if( instance->plugin.IsEffect() )
	{
		instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
		instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
		instance->input.Handle                                 = static_cast< GLuint >( clip );
		instance->process.numInputTextures                     = 1;
		instance->process.inputTextures                        = instance->inputs;
	}
	else
	{
		instance->process.numInputTextures = 0;
		instance->process.inputTextures    = nullptr;
	}
	instance->process.HostFBO = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one Mirrorball.cpp's ProcessOpenGL applies to it. NaN where the plugin
// uses the host's 0..1 as it is (Mix), or an index, or a count.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double mb_convert( Instance* instance, int index, float v )
{
	switch( idOf( instance, index ) )
	{
	case PT_DIAMETER: return DiameterFromParam( v );
	case PT_FACET_SIZE: return FacetFromParam( v );
	case PT_MISSING: return MissingFromParam( v );
	case PT_SPEED: return SpeedFromParam( v );
	case PT_STEP_ANGLE: return StepAngleFromParam( v );
	case PT_GLIDE: return GlideFromParam( v );
	case PT_BEAM: return BeamFromParam( v );
	case PT_THROW: return ThrowFromParam( v );
	case PT_ELEVATION: return ElevationFromParam( v );
	case PT_BEARING: return BearingFromParam( v );
	case PT_ROOM: return RoomFromParam( v );
	case PT_ROOM_LIGHT: return RoomLightFromParam( v );
	case PT_EXPOSURE: return ExposureFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion (the one its constructor
/// sets the defaults with). The page's presets are written in metres,
/// degrees and rpm and go through this. NaN for a parameter with no inverse
/// (Glide and Room Light are the host's 0..1 clamped, so a preset gives them
/// as that).
EMSCRIPTEN_KEEPALIVE double mb_param_from( Instance* instance, int index, double value )
{
	switch( idOf( instance, index ) )
	{
	case PT_DIAMETER: return ParamFromDiameter( value );
	case PT_FACET_SIZE: return ParamFromFacet( value );
	case PT_MISSING: return ParamFromMissing( value );
	case PT_SPEED: return ParamFromSpeed( value );
	case PT_STEP_ANGLE: return ParamFromStepAngle( value );
	case PT_BEAM: return ParamFromBeam( value );
	case PT_THROW: return ParamFromThrow( value );
	case PT_ELEVATION: return ParamFromElevation( value );
	case PT_BEARING: return ParamFromBearing( value );
	case PT_ROOM: return ParamFromRoom( value );
	case PT_EXPOSURE: return ParamFromExposure( value );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads (LastFrame(), MotorForTest(), HostNow()).
//---------------------------------------------------------------------------
/// The ball's phase the last frame drew with, turns in [0, 1).
EMSCRIPTEN_KEEPALIVE double mb_psi( Instance* instance )
{
	return instance->plugin.LastFrame().psi;
}
/// Whole turns plus the fraction, as the motor keeps it.
EMSCRIPTEN_KEEPALIVE double mb_turns( Instance* instance )
{
	return instance->plugin.MotorForTest().Unwrapped();
}
/// Sync's steps scheduled so far.
EMSCRIPTEN_KEEPALIVE int mb_steps( Instance* instance )
{
	return instance->plugin.MotorForTest().Steps();
}
/// The host's time as the plugin reads it, seconds (the wall clock until its
/// seconds-or-milliseconds vote settles).
EMSCRIPTEN_KEEPALIVE double mb_host_now( Instance* instance )
{
	return instance->plugin.HostNow();
}
EMSCRIPTEN_KEEPALIVE int mb_facets( Instance* instance )
{
	return instance->plugin.LastFrame().layout.total;
}
EMSCRIPTEN_KEEPALIVE int mb_rings( Instance* instance )
{
	return instance->plugin.LastFrame().layout.Rings();
}
/// The lamp's distance from the ball's centre after the room's clamp, metres.
EMSCRIPTEN_KEEPALIVE double mb_throw_used( Instance* instance )
{
	return instance->plugin.LastFrame().lamp.throwUsed;
}
EMSCRIPTEN_KEEPALIVE double mb_room_width( Instance* instance )
{
	return instance->plugin.LastFrame().room.width;
}
EMSCRIPTEN_KEEPALIVE int mb_frame( Instance* instance )
{
	return instance->plugin.LastFrame().frame;
}

/// Give the plugin's log (Diag.cpp) somewhere to go: a directory in the
/// page's in-memory file system, named through the override Diag.cpp reads.
/// Its own `mkdir -p` goes through system(), which a browser does not have.
/// Call before the first InitGL, which is when the log opens.
EMSCRIPTEN_KEEPALIVE void mb_prepare_log()
{
	setenv( "MIRRORBALL_LOG_DIR", "/mirrorball/logs", 1 );
	mkdir( "/mirrorball", 0755 );
	mkdir( "/mirrorball/logs", 0755 );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* mb_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
