//
// Created by Twiiz on 2024/1/20.
//

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if !defined(TARGET_OS_IPHONE)
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#endif