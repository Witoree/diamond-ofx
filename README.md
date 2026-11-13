# Diamond OFX

![Diamond OFX](docs/assets/diamond-banner.png)

Diamond OFX is a personal project. I study Game Design and started this because I wanted a camera shake effect for my VEGAS edits. I vibe coded it with AI and put the source here in case anyone wants to see how it works or mess with it. It started with one effect and now has 11.

Diamond is an open-source OpenFX plug-in suite for Windows x64, developed for VEGAS Pro 23 Steam Edition. Its 11 CPU-rendered effects work with still images and video.

**[Download the v0.03 prerelease](https://github.com/Witoree/diamond-ofx/releases/tag/v0.03)** · `Diamond-0.03-Win64.zip` · [Installation guide](docs/INSTALL.md)

I tried `D_Shake` from v0.01 in VEGAS Pro 23 Steam Edition and it worked for me. The v0.03 build is for Windows x64. I still need to try this version in VEGAS.

## Install

1. Close VEGAS and extract `Diamond-0.03-Win64.zip`.
2. If you're updating, move the old `Diamond.ofx.bundle` out of the plug-in directory first. Copy the new `Diamond.ofx.bundle` folder to `C:\Program Files\Common Files\OFX\Plugins\`.
3. Reopen VEGAS and look under **Video FX → Diamond**.

See the [installation guide](docs/INSTALL.md) for updates, removal, and troubleshooting.

## Effects

| Effect | Use |
| --- | --- |
| `D_Shake` | Handheld camera motion with horizontal and vertical displacement, rotation, zoom variation, and motion blur. |
| `D_LensFlare` | Anamorphic or spherical lens flare with streaks, chromatic halos, reflections, and a flare-only output. |
| `D_Glow` | Highlight bloom with a soft threshold, two diffusion scales, and Add or Screen blending. |
| `D_BlurMoCurves` | Motion blur driven by keyframed position, zoom, and rotation. Set the shutter duration, offset, and exposure bias. |
| `D_DigitalDamage` | Glitch effects with displaced bands, RGB separation, macroblocks, scanlines, and signal dropout. |
| `D_Rays` | Colored light rays generated from the bright parts of the clip. |
| `D_UltraZap` | Animated electrical arcs with branching, glow, and adjustable endpoints. |
| `D_PixelSort` | Sorts runs of pixels by luminance, color channel, or saturation along an adjustable direction. |
| `D_RackDefocus` | Optical defocus with circular, polygonal, or anamorphic bokeh and highlight gain. |
| `D_LightLeak` | Warm, cool, or golden light leaks with adjustable placement, falloff, and motion. |
| `D_FilmEffect` | Print-style grading with animated grain, halation, and vignette. |

## Build and contribute

Use Visual Studio 2022 or Visual Studio Build Tools with the **Desktop development with C++** workload. Run `build.cmd` from the repository root to create `build\Diamond.ofx.bundle`. [Build guide](docs/BUILD.md) · [Contributing](CONTRIBUTING.md) · [Changelog](CHANGELOG.md)

Diamond uses independent implementations informed by public effect documentation. Diamond's code is [MIT licensed](LICENSE.txt). The bundled OpenFX SDK headers retain their [BSD 3-Clause license](third_party/openfx/LICENSE.md). [Sapphire OpenFX effect reference](https://borisfx.com/documentation/sapphire/ofx/name-index/)
