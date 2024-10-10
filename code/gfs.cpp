
#include "gfs.h"

//===============================================================
// @Purpose: Test for rendering
//===============================================================
int32 PlayerWidth = 24;
int32 PlayerHeight = 24;

int32 PosX = 100;
int32 PosY = 100;


INTERNAL void OutputGameSound(gfs_sound_buffer* SoundBuffer)
{
    LOCALPERSIST real32 tSine;
    int16 ToneVolume = 3000;
    int ToneHz = 256;
    int WavePeriod = SoundBuffer->SamplesPerSecond / ToneHz;

    int16* SampleOut = SoundBuffer->Samples;
    for ( int SampleIndex = 0; SampleIndex < SoundBuffer->SampleCount; ++SampleIndex )
    {
        real32 SineValue = sinf( tSine );
        int16 SampleValue = (int16)(SineValue * ToneVolume);
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;

        tSine += 2.0f * PI32 * 1.0f / (real32)WavePeriod;
    }
}

INTERNAL void RenderWeirdPixelTest( gfs_offscreen_buffer* Buffer, int XOffset, int YOffset )
{
    uint8* Row = (uint8*)Buffer->Memory;

    for ( int Y = 0; Y < Buffer->Height; ++Y )
    {
        uint32* Pixel = (uint32*)Row;
        for ( int X = 0; X < Buffer->Width; ++X )
        {
            *Pixel++ = (((XOffset) << 16) | ((YOffset) << 8) | 128);
        }
        Row += Buffer->Pitch;
    }

    PosX += XOffset;
    PosY += -YOffset;

    if ( PosX < 0 ) PosX = 0;
    if ( PosY < 0 ) PosY = 0;

    if ( (PosX + PlayerWidth) >= Buffer->Width ) PosX = Buffer->Width - PlayerWidth - 2;
    if ( (PosY + PlayerHeight) >= Buffer->Height ) PosY = Buffer->Height - PlayerHeight - 2;
    
    Row = (uint8*)Buffer->Memory + (Buffer->Pitch * PosY);

    for ( int Y = 0; Y < PlayerHeight; ++Y )
    {
        uint32* Pixel = (uint32*)Row + PosX;
        for ( int X = 0; X < PlayerWidth; ++X )
        {
            *Pixel++ = ((0 << 16) | (0 << 8) | 0);
        }

        Row += Buffer->Pitch;
    }
    
}

INTERNAL void GameUpdateAndRender( gfs_offscreen_buffer* Buffer, int32 XOffset, int32 YOffset, gfs_sound_buffer* SoundBuffer )
{
    // TODO(oyvind): Allow sample offsets here for more robust platform options
    OutputGameSound( SoundBuffer);
    RenderWeirdPixelTest( Buffer, XOffset, YOffset );
}