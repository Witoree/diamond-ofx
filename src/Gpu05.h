#pragma once

#include "Effects.h"

namespace diamond::fx {

// The driver owns GPU selection and this API handles uploads/downloads from
// ordinary CPU-memory OFX renders. Returns false so the caller can render on CPU.
bool runGpu05(const Context& context, const char* programSource, const char* kernelName,
              const void* params, std::size_t paramBytes,
              const void* extra = nullptr, std::size_t extraBytes = 0);

// Called by the host's explicit unload action, outside the DLL loader lock.
void releaseGpu05();

} // namespace diamond::fx
