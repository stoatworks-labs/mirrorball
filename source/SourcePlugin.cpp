#include "Mirrorball.h"

/**
    The source: a mirror ball in a room, a pin spot on it, the gobo wheel's
    gobo in every spot.

    Listed directly in the MirrorballSource target, not in mirrorball_core:
    both plugins share the class and not the `CFFGLPluginInfo` below, and
    putting either registration in the shared library would register both
    plugins into both bundles. The core is an OBJECT library because this
    registers itself from a file-scope constructor nothing references.

    `SW Mirrorball` is thirteen characters; the FFGL name field is char[ 16 ]
    and not null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class MirrorballSource : public mirrorball::MirrorballPlugin
{
public:
	MirrorballSource() :
		MirrorballPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< MirrorballSource >,// Create method
	"MR01",                           // Plugin unique ID of maximum length 4
	"SW Mirrorball",                  // Plugin name
	2,                                // API major version number
	1,                                // API minor version number
	0,                                // Plugin major version number
	1,                                // Plugin minor version number
	FF_SOURCE,                        // Plugin type
	"A mirror ball throwing a pin spot round the room. Every facet is a flat mirror: its spot is the pin spot seen through "
	"a small window, a full copy of the gobo when the lamp is close and a mirrored piece of it when it is far. The spots "
	"turn as one body, crawling near the axis and racing at the edges.",
	"Mirrorball FFGL source"// About
);

extern "C" const char* MirrorballSourceBuildStamp()
{
	return "mirrorball " MIRRORBALL_VERSION " source, built " __DATE__ " " __TIME__;
}
