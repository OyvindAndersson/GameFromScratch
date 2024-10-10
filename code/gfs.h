#pragma once
/*===============================================================
 @Purpose: Programming very performant C/C++ game
 @Creator: Oyvind Andersson
 @Notice : Based on the Handmade Hero series, by Casey Muratori.
           This is not an all-purpose game-engine. This is hyper-
           focused on performance and to be educational by going
           "Down-to-the-metal" and being in full control of all
           aspects. Understanding how you'd do it in the ol' days
           to churn out performance, and from that having a base
           knowledge so as to be able to judge when/how an app
           is non-performant and what makes it so.

           Building is done using Unity ("Jumbo") build.
=================================================================*/
#include <stdint.h>
#include <math.h> // TODO(oyvind): Implement SIN ourselves

//===============================================================
// Defines | platform+
//===============================================================

#define INTERNAL static
#define GLOBALVAR static
#define LOCALPERSIST static

#define PI32 3.14159265359f

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int32 bool32;

typedef float real32;
typedef double real64;

/*
	NOTE(oyvind): Services that the game provides to the platform layer
*/

struct gfs_offscreen_buffer {
    void* Memory; // Pixels are always 32-bits wide, Mem order BB GG RR XX
    int Width;
    int Height;
    int Pitch;
};

struct gfs_sound_buffer {
    int SampleCount;
    int SamplesPerSecond;
    int16* Samples;
};

//===============================================================
// @Purpose: Game layer update and render call. Gets
// called by the platform layer main loop
// 
// Needs: timing, input controller/keyboard, bitmap buffer to use, sound buffer to use
//===============================================================
INTERNAL void GameUpdateAndRender( gfs_offscreen_buffer* Buffer, int32 XOffset, int32 YOffset, gfs_sound_buffer* SoundBuffer );
