// The miniaudio implementation, compiled once (see external/meson.build). Only playback through
// the low-level engine is used: no resource manager (it would start job threads), no encoding.
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
