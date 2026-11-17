#pragma once

#include "EffectsHost.h"
#include "ImageInImage07.h"

#include <ofxImageEffect.h>

namespace diamond::fx {

void describePictureControls(Parameters);
PictureSettings pictureSettingsAt(Parameters, OfxTime);
void registerPictureOverlay(const OfxHost*, const OfxImageEffectSuiteV1*, Parameters,
                            OfxPropertySetHandle descriptor);
bool pictureProjectArea(const OfxImageEffectSuiteV1*, const OfxPropertySuiteV1*,
                        OfxImageEffectHandle, OfxRectD&);

} // namespace diamond::fx
