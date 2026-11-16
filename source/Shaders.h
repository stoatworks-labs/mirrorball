#pragma once

#include <string>

/**
    The passes, once a frame.

        1. **gobo**   RGBA16F 512 x 512, mipmapped: the gobo's transmission in
                      linear light in the central 256 x 256 (a disc of radius 1
                      in gobo units, 128 texels), black around it. Built-in
                      patterns are drawn once per change; the Over's clip in
                      Gobo every frame (centre-cropped to a square,
                      premultiplied by alpha, masked by the round gate).
        2. **final**  the host's frame. For each pixel: the surface point (a
                      room face through the camera, or a plane), then the
                      GATHER -- the few facets whose reflection of the pin spot
                      could reach it, each traced back through its facet to
                      the pupil and the gate -- plus the spill past the ball,
                      the ball itself in the Room view, exposure, sRGB, and
                      the Over's composite.

    Assembled at InitGL as kVersion + kCommon + the pass by `Assemble()`, the
    one place the order is written; tools/glslc.sh's ASSEMBLED table mirrors
    it. No sin, cos, tan or atan built-in in the final pass: cosTurn and
    sinTurn are a reduced Taylor series (throw's), atan a polynomial.
*/
namespace mirrorball::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kGoboFragment;
extern const char* const kFinalFragment;

enum class Pass
{
	Gobo,
	Final
};

std::string Assemble( Pass pass );

/// Hook bits (the Hooks uniform). A shipped instance sets 0.
enum Hook : int
{
	kHookRaw          = 1 << 0, ///< rgb = irradiance, lux (linear, no exposure); a = 1
	kHookCoverage     = 1 << 1, ///< rgb = the sum of pupil x gate coverage, not light
	kHookSpotsOnly    = 1 << 2, ///< no spill, no room light, no ball
	kHookCount        = 1 << 3, ///< a = the candidates the gather visited
	kHookGoboOut      = 1 << 4, ///< rgb = the gobo the last covered facet (or the spill) sampled, a = its coverage
	kHookNormalSpot   = 1 << 5, ///< negative: the spot follows the facet's normal
	kHookNoFactor2    = 1 << 6, ///< negative: w - ( w . n ) n, the 2 dropped
	kHookVirtualApex  = 1 << 7, ///< negative: the gate cone's apex at the lamp's mirror image
	kHookNoWindow     = 1 << 8, ///< negative: the pupil ignored, every spot a full gate cone
	kHookNoFacetCos   = 1 << 9, ///< negative: the facet's solid angle without its cosine
	kHookNoInvSquare  = 1 << 10,///< negative: the facet's solid angle without 1 / D^2
	kHookDifferential = 1 << 11,///< negative: each ring turns at psi cos phi
	kHookUnmirrored   = 1 << 12,///< negative: the reflected gobo read unmirrored
	kHookAlphaThrough = 1 << 13,///< negative: the Over (Gobo) passes the clip's alpha through
	kHookAlphaUnmasked= 1 << 14,///< negative: the Over (Surface) keeps rgb under alpha 0
	kHookSaltByFrame  = 1 << 15 ///< negative: the missing facets' hash salted by the frame
};

constexpr int kGoboSize = 512;
/// Gobo texels per unit of the gobo coordinate g.
constexpr float kGoboTexelsPerUnit = 128.0f;
/// The gather's cap: at most ( 2 K + 1 )^2 candidates a pixel.
constexpr int kMaxWindow = 6;

} // namespace mirrorball::shaders
