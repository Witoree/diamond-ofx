# Diamond OFX

![Diamond OFX](docs/assets/diamond-banner.png)

Personal project. vibe coded this with AI because I wanted camera shake for my VEGAS edits and not spend 80 USD on a certain plugin.

**[Download v0.07.1](https://github.com/Witoree/diamond-ofx/releases/tag/v0.07.1)** for Windows x64. Close VEGAS and copy `Diamond.ofx.bundle` from the Win64 ZIP to `C:\Program Files\Common Files\OFX\Plugins\`. Reopen VEGAS and look under **Video FX → Diamond** or **Transitions → Diamond**. [All releases](https://github.com/Witoree/diamond-ofx/releases) · [Install guide](docs/INSTALL.md)

`D_Rays`, `D_BlurMoCurves`, and `D_EdgeRays` use OpenCL GPU acceleration with automatic CPU fallback.

## Effects

`D_Shake`, `D_LensFlare`, `D_Glow`, `D_BlurMoCurves`, `D_DigitalDamage`, `D_Rays`, `D_UltraZap`, `D_PixelSort`, `D_RackDefocus`, `D_LightLeak`, `D_FilmEffect`, `D_VHSDamage`, `D_FilmBurn`, `D_DistortRGB`, `D_WarpFishEye`, `D_WarpWaves`, `D_Glare`, `D_Glint`, `D_Streaks`, `D_HalfTone`, `D_BleachBypass`, `D_Blur`, `D_BlurDirectional`, `D_Sharpen`, `D_Vignette`, `D_Grain`, `D_Halation`, `D_GlowAura`, `D_GlowDarks`, `D_GlowDist`, `D_GlowEdges`, `D_GlowNoise`, `D_GlowOrthicon`, `D_GlowRainbow`, `D_GlowRings`, `D_UltraGlow`, `D_EdgeRays`, `D_GlintRainbow`, `D_BokehLights`, `D_DropShadow`, `D_Flashbulbs`, `D_Light3D`, `D_SpotLight`, `D_Hotspots`, `D_FilmDamage`, `D_GrainStatic`, `D_UltraGrain`, and `D_ImageInImage`.

In VEGAS, place `D_ImageInImage` after **Pan/Crop** in the Event FX chain. Turn on **Perspective** to animate its corners. The preview outline, corner grips, and rotation ring appear on hosts with OpenFX overlay support. `D_Light3D` accepts a **Normals** clip in the OpenFX General context.

## Transitions

`D_Dissolve`, `D_DissolveGlow`, `D_DissolveShake`, and `D_SwishPan`.

[Changelog](CHANGELOG.md) · [Build](docs/BUILD.md) · [Contribute](CONTRIBUTING.md) · [MIT license](LICENSE.txt) · [OpenFX license](third_party/openfx/LICENSE.md)
