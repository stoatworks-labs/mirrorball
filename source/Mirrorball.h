#pragma once

#include "Ball.h"
#include "Controls.h"
#include "Motor.h"
#include "PassBuffer.h"
#include "Shaders.h"

#include <FFGLSDK.h>

#include "StoatworksAboutParams.h"

#include <vector>

namespace mirrorball
{

/// The exposure's reference: a spot of E_ref lux on a white wall reads as
/// white at Exposure 0 (a choice: a default spot on the back wall reads at
/// about 0.8 of white; SPEC-mirrorball.md).
constexpr double kReferenceLux = 3.0;

/// What one frame drew with: the harness reads it to build its closed forms.
struct FrameData
{
	ball::Layout layout;
	lamp::Lamp lamp;
	room::Room room;
	room::View view;
	double psi       = 0.0;///< turns, [0, 1)
	double gain      = 1.0;///< 2^Exposure / E_ref
	double roomLight = 0.0;
	int width = 0, height = 0;
	int kMax  = shaders::kMaxWindow;
	uint32_t missingBelow = 0;
	int seed  = 0;
	int frame = 0;
};

/// A test rig: the view, the lamp and the phase put exactly where a check
/// wants them. Never set by a host.
struct TestRig
{
	bool plane = false;///< an orthographic plane instead of the view
	Vec3 centre {}, normal { 0.0, 0.0, 1.0 }, upHint { 0.0, 1.0, 0.0 };
	double halfHeight = 1.0;
	bool lamp         = false;///< the lamp exactly here, aimed at the ball
	Vec3 lampPos {};
	bool psi        = false;///< the motor bypassed
	double psiTurns = 0.0;
	int soloRing = -1, soloIndex = -1;///< one facet; ring -2 is none (the spill alone)
	int kMax     = shaders::kMaxWindow;
};

/// Plugin-level negative controls.
enum PluginHook : int
{
	kPluginResetOnResize = 1 << 0///< the motor reset when the frame changes size
};

/**
    The plugin: the source (`SW Mirrorball`) and, with `isEffect`, the Over
    (`SW Mirror Over`, the spots on the clip, or the clip as the gobo). One
    class, two registrations; see SourcePlugin.cpp and EffectPlugin.cpp.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate.
*/
class MirrorballPlugin : public CFFGLPlugin
{
public:
	explicit MirrorballPlugin( bool isEffect );

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//The harness's hooks. A host never calls these.
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	void SetMotorHooksForTest( int bits )
	{
		motor.SetHooksForTest( bits );
	}
	void SetPluginHooksForTest( int bits )
	{
		pluginHooks = bits;
	}
	void SetRigForTest( const TestRig& rig )
	{
		testRig = rig;
	}
	const FrameData& LastFrame() const
	{
		return frame;
	}
	const MotorState& MotorForTest() const
	{
		return motor;
	}
	double HostNow() const
	{
		return now;
	}

private:
	void UpdateClock();
	void drawGobo( const FFGLTextureStruct* input, int kind );
	void uploadRings();
	void drawFinal( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );
	void uniform( const ffglex::FFGLShader& shader, const char* name, int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b );
	void uniform( const ffglex::FFGLShader& shader, const char* name, const Vec3& v );
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader goboShader, finalShader;
	ffglex::FFGLScreenQuad quad;
	PassBuffer gobo;     ///< RGBA16F, mipmapped
	PassBuffer ringTable;///< RGBA32F kMaxRings x 1: sin phi, cos phi, N, offset
	int goboBuilt = -1;
	double ringsDiameter = -1.0, ringsFacet = -1.0;

	MotorState motor;
	FrameData frame;
	TestRig testRig;
	int lastWidth = 0, lastHeight = 0;

	//The clock (double throughout: Resolume's has been seen at ~499 million ms).
	double hostTime     = -1.0;
	double now          = 0.0;
	double lastNow      = -1.0;
	double clockScale   = 0.0;
	double wallStart    = -1.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	int secondsVotes = 0, millisVotes = 0, clockFrames = 0;
	bool settledJump = false;

	float bpm          = 120.0f;
	float barPhase     = 0.0f;
	float lastBarPhase = -1.0f;
	bool hostBeatSeen  = false;

	int hooks       = 0;
	int pluginHooks = 0;
};

} // namespace mirrorball
