# Changelog

## 0.06

- Added ten lighting effects, including UltraGlow and GPU-accelerated EdgeRays.
- Integrated Marcel's CPU rendering, OpenCL device selection, and OpenFX test harness changes.

## 0.03

Added five effects, bringing the total to eleven. The six effects from 0.02 retain their identifiers, controls, and renderers.

- `D_UltraZap` generates animated lightning with branching arcs, a luminous core, and a halo.
- `D_PixelSort` sorts runs of pixels by luminance, color channel, or saturation.
- `D_RackDefocus` simulates bokeh with a circular, polygonal, or anamorphic iris.
- `D_LightLeak` simulates light leaks with three profiles and Add or Screen compositing.
- `D_FilmEffect` simulates film looks with color profiles, animated grain, halation, and vignette.

## 0.02

Added five effects and retained `D_Shake` from 0.01.

- `D_LensFlare` generates optical ghosts, halos, and light streaks in three styles.
- `D_Glow` applies bloom to highlights at two scales.
- `D_BlurMoCurves` generates motion blur from position, zoom, and rotation curves.
- `D_DigitalDamage` simulates signal breakup, RGB channel separation, and macroblocking.
- `D_Rays` casts beams from image highlights.

## 0.01

- Introduced `D_Shake` with seeded, repeatable camera movement, translation, rotation, zoom variation, overscan, motion blur, and edge modes.
- Packaged the OpenFX bundle for Windows x64, targeting VEGAS Pro 23 Steam Edition.
