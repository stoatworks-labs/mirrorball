#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# One scratch copy of the tree (the FFGL SDK symlinked), configured once. For
# each mutant: the file restored and mutated, this repo's object files AND the
# mbtest binary deleted (a stale object once passed for a mutant), mbtest
# rebuilt arm64-only, the mutant proved to be IN the binary (a GLSL mutant's
# text is in it verbatim; a C++ mutant's binary must differ from the
# unmutated build's), then the named check run under a time limit (a mutant
# can hang; a timeout counts as caught).
#
#     tools/mutate.sh            every mutant
#     ONLY=7 tools/mutate.sh     the seventh alone
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The scratch tree goes under TMPDIR when it is set (a heredoc into python
# hung once with the system's /var/folders TMPDIR: the fleet's trap).
WORK="$(mktemp -d "${TMPDIR:-/tmp}/mbmutate.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check | the raster | what it is
# Split on '|', so neither a target nor a mutant may contain one (conway's
# trap): no || in a mutant.
MUTANTS=(
	"source/Shaders.cpp|w = u - 2.0 * un * n;|w = u - 3.0 * un * n;|--reflect|320x180|GLSL: reflection's factor 2 made 3, 2 -> 3"
	"source/Shaders.cpp|float th = ( float( j ) + ring.w ) / float( Nk ) + ( hooked|float th = ( float( j ) - ring.w ) / float( Nk ) + ( hooked|--reflect|320x180|GLSL: the facets placed against the ring's offset, + -> -"
	"source/Ball.cpp|return { ring.cosPhi * CosTurn( t ), ring.sinPhi, -ring.cosPhi * SinTurn( t ) };|return { ring.cosPhi * CosTurn( t ), ring.sinPhi, +ring.cosPhi * SinTurn( t ) };|--reflect|320x180|C++: the CPU's facet normals mirrored (the GPU's are not), - -> +"
	"source/Shaders.cpp|float cg  = clamp( ( rampEdge( 1.0, wg ) - length( g ) ) / ( 2.0 * wg ) + 0.5, 0.0, 1.0 );|float cg  = clamp( ( rampEdge( 1.0, wg ) - length( g ) ) / ( 2.0 * wg ) + 0.7, 0.0, 1.0 );|--area|320x180|GLSL: the gate's half-light contour moved out by a fifth of its ramp, 5 -> 7"
	"source/Shaders.cpp|float wp = 0.5 * FacetSize * ( D + ts ) / D + 0.5 * foot * ts / D;|float wp = 0.5 * FacetSize * ( D - ts ) / D + 0.5 * foot * ts / D;|--area|320x180|GLSL: the facet's ramp on the pupil from D - L, + -> -"
	"source/Ball.h|constexpr double kPupilDiameter = 0.114;|constexpr double kPupilDiameter = 0.115;|--flux|320x180|C++: the pupil a millimetre wide of the PAR36's (the near field's luminance 1.7% low), 4 -> 5"
	"source/Shaders.cpp|return sqrt( r2 - w2 / 3.0 );|return sqrt( r2 - w2 / 4.0 );|--flux|320x180|GLSL: the ramp's edge set for w^2/4, not w^2/3, 3 -> 4"
	"source/Shaders.cpp|return pow( 6.0 * w * r2, 1.0 / 3.0 ) - w;|return pow( 7.0 * w * r2, 1.0 / 3.0 ) - w;|--flux|320x180|GLSL: the wide ramp's edge (the second branch) from 7 w r^2, 6 -> 7"
	"source/Shaders.cpp|( hooked( kHookNoInvSquare ) ? 1.0 : D * D );|( hooked( kHookNoInvSquare ) ? 1.0 : D + D );|--flux|320x180|GLSL: the facet's solid angle over 2D, not D^2, * -> +"
	"source/Shaders.cpp|float cosP  = max( dot( NP, -u ), 0.0 );|float cosP  = max( dot( NP, +u ), 0.0 );|--flux|320x180|GLSL: the wall's cosine from behind it, - -> +"
	"source/Ball.h|constexpr double kChauvetLux = 4120.0|constexpr double kChauvetLux = 4130.0|--flux|320x180|C++: the Chauvet anchor misread, 2 -> 3"
	"source/Ball.h|constexpr double kReflectance = 0.86;|constexpr double kReflectance = 0.88;|--flux|320x180|C++: the facet's reflectance 0.88, 6 -> 8"
	"source/Shaders.cpp|float thw = atan2Turn( -ns.z, ns.x ) - Psi;|float thw = atan2Turn( -ns.z, ns.x ) + Psi;|--circles|320x180|GLSL: the gather's window turned against the ball, - -> +"
	"source/Shaders.cpp|vec2 g  = vec2( dot( w, LampRight ), dot( w, LampUp ) ) / ( den * TanAlpha );|vec2 g  = vec2(-dot( w, LampRight ), dot( w, LampUp ) ) / ( den * TanAlpha );|--window|320x180|GLSL: the reflected gobo read unmirrored, ' ' -> -"
	"source/Shaders.cpp|vec2 g   = vec2( dot( w, LampRight ), dot( w, LampUp ) ) / ( den * TanAlpha );|vec2 g   = vec2(-dot( w, LampRight ), dot( w, LampUp ) ) / ( den * TanAlpha );|--window|320x180|GLSL: the spill's gobo mirrored, ' ' -> -"
	"source/Shaders.cpp|return pcg( uint( ring ) * 7919u + pcg( uint( index ) + pcg( uint( seed ) + 0x68E31DA4u ) ) );|return pcg( uint( ring ) * 7919u + pcg( uint( index ) + pcg( uint( seed ) + 0x68E31DA5u ) ) );|--missing|320x180|GLSL: the GPU's missing hash salted differently from the CPU's, 4 -> 5"
	"source/Ball.cpp|+ Pcg( static_cast< uint32_t >( index ) + Pcg( static_cast< uint32_t >( seed ) + 0x68E31DA4u ) ) );|+ Pcg( static_cast< uint32_t >( index ) + Pcg( static_cast< uint32_t >( seed ) + 0x68E31DA5u ) ) );|--missing|320x180|C++: the CPU's missing hash salted differently from the GPU's, 4 -> 5"
	"source/Motor.cpp|fold( in.rpm / 60.0 * ( ( hooks & kMotorPerFrame ) ? 1.0 / 60.0 : dt ) );|fold( in.rpm / 50.0 * ( ( hooks & kMotorPerFrame ) ? 1.0 / 60.0 : dt ) );|--rate|320x180|C++: Free's rpm over 50 s, 6 -> 5"
	"source/Controls.cpp|constexpr double kSpeedHigh = 5.0;|constexpr double kSpeedHigh = 6.0;|--rate|320x180|C++: Speed's range +-6 rpm, not +-5, 5 -> 6"
	"source/Motor.cpp|const double inside = ( boundary - p0 ) / ( p1 - p0 );|const double inside = ( boundary + p0 ) / ( p1 - p0 );|--sync|320x180|C++: the beat's place in its frame from the sum of phases, - -> +"
	"source/Motor.cpp|return 0.5 - 0.5 * std::cos( kPi * x );|return 0.6 - 0.5 * std::cos( kPi * x );|--sync|320x180|C++: the glide's raised cosine starting at 0.1, 5 -> 6"
	"source/Motor.cpp|moves.push_back( { start, std::max( 0.0, in.glide ) * step * 60.0 / bpm, turns } );|moves.push_back( { start, std::max( 0.0, in.glide ) * step * 50.0 / bpm, turns } );|--sync|320x180|C++: the glide's length from 50 s a minute, 6 -> 5"
	"source/Motor.cpp|ownBeats += bpm / 60.0 * dt;|ownBeats += bpm / 50.0 * dt;|--sync|320x180|C++: the own clock's beats at 50 s a minute, 6 -> 5"
	"source/Shaders.cpp|sum += ( wx * wy * clamp( t.a, 0.0, 1.0 ) ) * toLinear( clamp( t.rgb, 0.0, 1.0 ) );|sum += ( wx * wy * clamp( t.a, 1.0, 1.0 ) ) * toLinear( clamp( t.rgb, 0.0, 1.0 ) );|--alpha|320x180|GLSL: the gobo ignores the clip's alpha, 0 -> 1"
	"source/Shaders.cpp|float aOut = hooked( kHookAlphaThrough ) ? a : mix( a, 1.0, m );|float aOut = hooked( kHookAlphaThrough ) ? a : mix( a, 0.0, m );|--alpha|320x180|GLSL: the Over's alpha mixed toward clear, 1 -> 0"
	"source/Shaders.cpp|if( a <= 0.0 && !hooked( kHookAlphaUnmasked ) )|if( a <= 1.0 && !hooked( kHookAlphaUnmasked ) )|--alpha|320x180|GLSL: Surface blanks every pixel, not only the clear ones, 0 -> 1"
	"source/Ball.h|constexpr double kPitchFactor = 1.1;|constexpr double kPitchFactor = 1.2;|--layout|320x180|C++: the facets 20% apart, not 10%, 1 -> 2"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null

clean_build() {
	find "$tree/build/CMakeFiles/mirrorball_core.dir" "$tree/build/CMakeFiles/mbtest.dir" -name '*.o' -delete 2>/dev/null || true
	rm -f "$tree/build/mbtest"
	cmake --build "$tree/build" --target mbtest -j4 >"$WORK/build.log" 2>&1
}

# The unmutated binary, for the C++ mutants' "is it in there" test.
clean_build || { tail -20 "$WORK/build.log"; echo "the unmutated tree does not build"; exit 1; }
cp "$tree/build/mbtest" "$WORK/baseline"

caught=0
total=0
index=0
for entry in "${MUTANTS[@]}"; do
	index=$(( index + 1 ))
	if [[ -n "${ONLY:-}" && "$ONLY" != "$index" ]]; then
		continue
	fi
	total=$(( total + 1 ))
	IFS='|' read -r file original mutant check raster what <<<"$entry"
	# Every file back as it is in the repo, then this one mutated.
	cp "$REPO/source/"* "$tree/source/"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if len(original) != len(mutant) or sum(a != b for a, b in zip(original, mutant)) != 1:
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant %d: %s\n' "$index" "$what"
	if ! clean_build; then
		# A mutant that does not compile proves nothing about the checks.
		printf '   FAIL  the mutant does not build -- choose another\n'
		tail -5 "$WORK/build.log" | sed 's/^/        /'
		continue
	fi
	# Is the mutant in the binary?
	if [[ "$file" == *Shaders.cpp ]]; then
		if ! LC_ALL=C grep -qF -- "$mutant" "$tree/build/mbtest"; then
			printf '   FAIL  the mutated GLSL is not in the binary\n'
			continue
		fi
	elif cmp -s "$tree/build/mbtest" "$WORK/baseline"; then
		printf '   FAIL  the binary is the unmutated one\n'
		continue
	fi
	status=0
	limit 600 "$tree/build/mbtest" "$check" --size "$raster" >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES at %s -- the check does not cover this code\n' "$check" "$raster"
	else
		printf '   ok    %s fails against the mutant at %s (exit %d):\n' "$check" "$raster" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "$total" "$caught"
[[ "$caught" -eq "$total" ]]
