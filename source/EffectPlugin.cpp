#include "Mirrorball.h"

/**
    The effect: the spots land on the clip (the clip is the back wall), or
    the clip is the pin spot's gobo and every spot carries a piece of it.

    See SourcePlugin.cpp for why this file is listed in its own target.
    `SW Mirror Over` is fourteen characters, inside FFGL's 16.
*/
namespace
{
class MirrorballEffect : public mirrorball::MirrorballPlugin
{
public:
	MirrorballEffect() :
		MirrorballPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< MirrorballEffect >,// Create method
	"MR02",                           // Plugin unique ID of maximum length 4
	"SW Mirror Over",                 // Plugin name
	2,                                // API major version number
	1,                                // API minor version number
	0,                                // Plugin major version number
	1,                                // Plugin minor version number
	FF_EFFECT,                        // Plugin type
	"A mirror ball's spots turning across your clip, as if the clip were the wall they land on. Or the clip as the pin "
	"spot's gobo: each spot then carries a mirrored piece of it, or the whole clip when the lamp is close.",
	"Mirrorball FFGL effect"// About
);

extern "C" const char* MirrorballEffectBuildStamp()
{
	return "mirrorball " MIRRORBALL_VERSION " effect, built " __DATE__ " " __TIME__;
}
