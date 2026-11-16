# Attributions

Mirrorball is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is a provisional hand copy: the master lists live in the `stoatworks-backend` repo and
are pushed out by `scripts/sync-attributions.py` once a project is registered there.
Mirrorball is not registered yet, so this file was written by hand on 2026-11-16, adapted
from haze's, and will be replaced by the generated one.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The plugins' shape, the harness and the tooling — Stoatworks haze, from ferrofluid

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

Two plugins on one OBJECT core with an `isEffect` flag, the table of controls, the host-clock vote, the sRGB encode, finding a beat's crossing inside the frame (ferrofluid's Drive), the harness's host, PNG writer, look card (schlieren's), cue sheets, `--pipe`/`--film` (SIGPIPE ignored, a closed stdout exits 1), `--negative` with its perturbation hooks, `--state`, `--names`, `--cues`, `--offline` and `--bench`, and tools/verify.sh, tools/mutate.sh, tools/glslc.sh, tools/sweep.py and scripts/release-lib.sh are haze's (ferrofluid's before it), adapted. tools/clips.py's decode and alpha check are convergence's, adapted.

### The pass buffers and the log — Stoatworks millpond

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} (FFGLFBO with the SDK's colour-texture leak fixed) and source/Diag.{h,cpp} are millpond's, by way of haze. Adapted.

### The GL state guard — Stoatworks vectrix, from resolume-scopes

<https://github.com/stoatworks-labs/vectrix>  
Licence: MIT  
Copyright: Stoatworks Labs

source/GLState.h is vectrix's, which lifted it from `resolume-scopes`' Scopes.cpp: the host's GL state captured and put back.

### The About block — Stoatworks xenon

<https://github.com/stoatworks-labs/xenon>  
Licence: MIT  
Copyright: Stoatworks Labs

source/StoatworksAbout.h, StoatworksAboutLinks.h and StoatworksAboutParams.h are provisional hand copies of xenon's, by way of haze, with `guide` empty (no user guide exists).

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Manufacturers' sheets for the ball, the motor and the pin spot

Steinigke/Eurolite's data sheets 5010030A, 5010110A, 50101111 and 5010150A (ball diameters, facet sizes, glass facets), the MD-2010 motor sheet (1 rpm, forward and backward) and the 50101855 set (a PAR-36 pin spot with a 20 cm ball); Stairville's MBM40D page (0.5–5 rpm); Chauvet DJ's LED Pinspot 2 specifications (4,120 lux at 2 m with the 6° lens), the photometric anchor; Eurolite's LED PST-5 QCL sheet (5° beam); the Tungsram sheet and lamptech's page for the GE 4515 PAR36 (its 114 mm diameter, the pupil); AGC's Mirox sheet quoting EN 1036 (reflectance 0.86). The spec lists each with what was read from it; `mbtest --flux` writes those literals in the harness itself and checks the plugin against them, not against its own constants.

### Textbook optics

M. Pharr, W. Jakob and G. Humphreys, *Physically Based Rendering*, 4th ed. (pbr-book.org), §9.3.3 (specular reflection) and §4.1.1 (radiance along a ray); OpenStax *University Physics* volumes 1 and 3 (v = rω; the law of reflection; plane-mirror images); Wikipedia's "Cross section (physics)" (a spherical mirror scatters a parallel beam isotropically). What `--reflect`, `--area`, `--flux`, `--circles` and `--window` measure the picture against.

## Standards and published specifications

What the implementation is measured against.

- **EN 1036** — mirrors from silver-coated float glass: the 0.86 reflectance floor, as quoted by AGC.
- **IEC 61966-2-1 (sRGB)** — the encode and decode in source/Shaders.cpp.
- **PCG** — M. E. O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation" (2014): the integer hash that picks the missing facets, the same on the CPU and the GPU.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### The mirror ball and its pin spot

Every disco, ballroom and school hall: one narrow lamp on a slow-turning ball, and the room full of moving squares of light. Built from how the thing works; nothing is copied from anyone's software, visualiser or fixture profile.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
