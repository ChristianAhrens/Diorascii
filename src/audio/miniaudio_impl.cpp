/// @file audio/miniaudio_impl.cpp
/// @brief Compiles the vendored miniaudio library (playback only), kept apart so its
/// warnings and build time stay out of the rest of the code.

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
