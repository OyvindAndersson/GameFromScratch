/*===============================================================
 @Purpose: Programming very performant C/C++ game
 @Creator: Oyvind Andersson
 @Notice : Based on the Handmade Hero series, by Casey Muratori.
=================================================================*/
/*
    TODO(oyvind): This is not a final platform layer
    - Saved game locations
    - Getting a handle to our own exe file
    - Asset loading path (where to load from)
    - Threading (launch a trhead)
    - Raw input (support for multiple keyboards)
    - Sleep/timeBeginPeriod
    - ClipCursor() (for multimonitor support)
    - Fullscreen support
    - WM_SETCURSOR (control cursor visibilty)
    - QueryCancelAutoplay
    - WM_ACTIVEAPP (for when we are not the active app)
    - Blit speed improvements (BitBlt)
    - HW acceleration (OpenGL or Direct3D or both?)
    - GetKeyboardLayout (for french keyboards, international WASD support)

    This is just a partial list so far..
*/

#include "gfs.cpp"

#include <windows.h>
#include <Xinput.h>
#include <dsound.h>
#include <stdio.h>


//===============================================================
// Structures
//===============================================================

struct win32_offscreen_buffer {
    BITMAPINFO Info;
    void *Memory;
    int Width;
    int Height;
    int Pitch;
    // NOTE(oyvind): Pixels are always 32-bits wide, mem order BB GG RR XX
};

struct win32_window_dimension
{
    int Width;
    int Height;
};

struct win32_sound_output
{
    // NOTE(oyvind): Sound test
    int SamplesPerSecond;
    int ToneHz;
    int ToneVolume;
    uint32 RunningSampleIndex;
    int WavePeriod;
    int BytesPerSample;
    int32 SecondaryBufferSize;
    real32 tSine;
    int LatencySampleCount;
};

//===============================================================
// Dynamically loaded Xinput and DirectSound
// (ref ca 25min https://www.youtube.com/watch?v=J3y1x54vyIQ&t=1684s)
//===============================================================

// NOTE(oyvind): Support for XInputGetState 
#define X_INPUT_GET_STATE(name) DWORD WINAPI name( DWORD dwUserIndex, XINPUT_STATE* pState )
typedef X_INPUT_GET_STATE( x_input_get_state );
X_INPUT_GET_STATE( XInputGetStateStub )
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
GLOBALVAR x_input_get_state* XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

// NOTE(oyvind): Support for XInputSetState
#define X_INPUT_SET_STATE(name) DWORD WINAPI name( DWORD dwUserIndex, XINPUT_VIBRATION* pVibration )
typedef X_INPUT_SET_STATE( x_input_set_state );
X_INPUT_SET_STATE( XInputGetStateStub )
{
    return ERROR_DEVICE_NOT_CONNECTED;
}
GLOBALVAR x_input_set_state* XInputSetState_ = XInputGetStateStub;
#define XInputSetState XInputSetState_

// NOTE(oyvind): Support for DirectSoundCreate
#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE( direct_sound_create );

//===============================================================
// Variables
//===============================================================

GLOBALVAR bool32 GlobalRunning;
GLOBALVAR win32_offscreen_buffer GlobalBackBuffer;
GLOBALVAR LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;

//===============================================================
// Helper functions
//===============================================================

INTERNAL win32_window_dimension Win32GetWindowDimension(HWND Window)
{
    win32_window_dimension Result;
    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return Result;
}

INTERNAL void ClearToBlack(win32_offscreen_buffer Buffer)
{
    uint8 *Row = (uint8 *)Buffer.Memory;

    for(int Y = 0; Y < Buffer.Height; ++Y)
    {
        uint32 *Pixel = (uint32 *)Row;
        for( int X = 0; X < Buffer.Width; ++X)
        {
            *Pixel++ = 0;
        }
        Row += Buffer.Pitch;
    }
}

INTERNAL void Win32ResizeDIBSection(win32_offscreen_buffer* Buffer, int Width, int Height)
{
    // TODO(oyvind): Maybe don't free first, free after, then free first if that fails.

    if(Buffer->Memory)
    {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;
    int BytesPerPixel = 4;

    // NOTE(oyvind): When biHeight is negative, that clues Windows to treat this bitmap as top-down.
    // The first three bytes of the image are the color for the top left pixel in the bitmap, not bottom-left!
    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height;
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapImageMemorySize = Buffer->Width * Buffer->Height * BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapImageMemorySize, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Buffer->Width * BytesPerPixel;

    ClearToBlack( GlobalBackBuffer );
}

INTERNAL void Win32DisplayBufferInWindow(HDC DeviceContext, win32_offscreen_buffer Buffer, int WindowWidth, int WindowHeight)
{
    // TODO(oyvind): Aspect ratio correction
    // TODO(oyvind): Play with stretch modes
    StretchDIBits(DeviceContext,
        0, 0, WindowWidth, WindowHeight,   // Dest
        0, 0, Buffer.Width, Buffer.Height, // Src
        Buffer.Memory,
        &Buffer.Info,
        DIB_RGB_COLORS, SRCCOPY );
}

INTERNAL void LoadXInput()
{
    // TODO(oyvind): Test this on windows 8 (win 8 may only have 1_3, and 7 only 1_3..)
    HMODULE XInputLibrary = LoadLibraryA( "xinput1_4.dll" );
    if ( !XInputLibrary )
    {
        XInputLibrary = LoadLibraryA( "xinput9_1_0.dll" );
    }

    if ( !XInputLibrary )
    {
        XInputLibrary = LoadLibraryA( "xinput1_3.dll" );
    }

    if ( XInputLibrary )
    {
        XInputGetState = (x_input_get_state*)GetProcAddress( XInputLibrary, "XInputGetState" );
        XInputSetState = (x_input_set_state*)GetProcAddress( XInputLibrary, "XInputSetState" );
    }
    else
    {
        // TODO(oyvind): Logging
    }
}

INTERNAL void Win32InitSound( HWND Window, int32 SamplesPerSecond, int32 BufferSize )
{
    // NOTE(oyvind): Load the lib
    HMODULE DSoundLibrary = LoadLibraryA( "dsound.dll" );

    if ( DSoundLibrary )
    {
        direct_sound_create* DirectSoundCreate = (direct_sound_create*)GetProcAddress( DSoundLibrary, "DirectSoundCreate" );

        LPDIRECTSOUND DirectSound;
        if ( DirectSoundCreate && SUCCEEDED( DirectSoundCreate( 0, &DirectSound, 0 ) ) )
        {
            //OutputDebugStringA( "DSound 1: Direct Sound created!\n" );
            WAVEFORMATEX WaveFormat = {};
            WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
            WaveFormat.nChannels = 2;
            WaveFormat.nSamplesPerSec = SamplesPerSecond;
            WaveFormat.wBitsPerSample = 16;
            WaveFormat.nBlockAlign = (WaveFormat.nChannels * WaveFormat.wBitsPerSample) / 8;
            WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec * WaveFormat.nBlockAlign;
            WaveFormat.cbSize = 0;

            if ( SUCCEEDED( DirectSound->SetCooperativeLevel( Window, DSSCL_PRIORITY ) ) )
            {
                //OutputDebugStringA( "DSound 2: Cooperative level set!\n" );
                DSBUFFERDESC BufferDescription = {};
                BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;
                BufferDescription.dwSize = sizeof( BufferDescription );

                LPDIRECTSOUNDBUFFER PrimaryBuffer;
                if ( SUCCEEDED( DirectSound->CreateSoundBuffer( &BufferDescription, &PrimaryBuffer, 0 ) ) )
                {
                    //OutputDebugStringA( "DSound 3: Primary sound buffer created!\n" );

                    if ( SUCCEEDED( PrimaryBuffer->SetFormat( &WaveFormat ) ) )
                    {
                        //OutputDebugStringA( "DSound 4: Set format on primary buffer succeeded!\n" );
                    }
                    else
                    {
                        // TODO(oyvind): Diagnostics
                    }
                }
            }
            else
            {
                // TODO(oyvind): Diagnostics
            }

            DSBUFFERDESC BufferDescription = {};
            BufferDescription.dwFlags = 0;
            BufferDescription.dwSize = sizeof( BufferDescription );
            BufferDescription.dwBufferBytes = BufferSize;
            BufferDescription.lpwfxFormat = &WaveFormat;

            if ( SUCCEEDED( DirectSound->CreateSoundBuffer( &BufferDescription, &GlobalSecondaryBuffer, 0 ) ) )
            {
                // NOTE(oyvind): Start playing
                //OutputDebugStringA( "DSound 5: Secondary soundbuffer created!\n" );
            }

        }
        else
        {
            // TODO(oyvind): Diagnostics
        }
    }
    else
    {
        // TODO(oyvind): Logging
    }
}

INTERNAL void Win32ClearBuffer( win32_sound_output* SoundOutput )
{
    VOID* Region1;
    DWORD Region1Size;
    VOID* Region2;
    DWORD Region2Size;

    if ( SUCCEEDED( GlobalSecondaryBuffer->Lock( 0, SoundOutput->SecondaryBufferSize,
        &Region1, &Region1Size,
        &Region2, &Region2Size,
        0 ) ) )
    {
        uint8* DestSample = (uint8*)Region1;
        for ( DWORD ByteIndex = 0; ByteIndex < Region1Size; ++ByteIndex )
        {
            *DestSample++ = 0;
        }

        DestSample = (uint8*)Region2;
        for ( DWORD ByteIndex = 0; ByteIndex < Region2Size; ++ByteIndex )
        {
            *DestSample++ = 0;
        }

        GlobalSecondaryBuffer->Unlock( Region1, Region1Size, Region2, Region2Size );
    }
}

INTERNAL void Win32FillSoundBuffer( win32_sound_output* SoundOutput, DWORD BytesToLock, DWORD BytesToWrite, gfs_sound_buffer* SourceBuffer )
{
    VOID* Region1;
    DWORD Region1Size;
    VOID* Region2;
    DWORD Region2Size;

    if ( SUCCEEDED( GlobalSecondaryBuffer->Lock( BytesToLock, BytesToWrite,
        &Region1, &Region1Size,
        &Region2, &Region2Size,
        0 ) ) )
    {
        // TODO(oyvind): Collapse these two loops
        DWORD Region1SampleCount = Region1Size / SoundOutput->BytesPerSample;
        int16* DestSample = (int16*)Region1;
        int16* SourceSample = (int16*)SourceBuffer->Samples;

        for ( DWORD SampleIndex = 0; SampleIndex < Region1SampleCount; ++SampleIndex )
        {
            *DestSample++ = *SourceSample++; // Ch1
            *DestSample++ = *SourceSample++; // Ch2
            ++SoundOutput->RunningSampleIndex;
        }

        DWORD Region2SampleCount = Region2Size / SoundOutput->BytesPerSample;
        DestSample = (int16*)Region2;
        for ( DWORD SampleIndex = 0; SampleIndex < Region2SampleCount; ++SampleIndex )
        {
            *DestSample++ = *SourceSample++; // Ch1
            *DestSample++ = *SourceSample++; // Ch2
            ++SoundOutput->RunningSampleIndex;
        }

        GlobalSecondaryBuffer->Unlock( Region1, Region1Size, Region2, Region2Size );
    }
}


//===============================================================
// Winapi callbacks
//===============================================================

INTERNAL LRESULT CALLBACK Win32WindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;
    switch(Message)
    {
        case WM_DESTROY:
        {
            // TODO Handle as error, recreate window?
            GlobalRunning = false;
            OutputDebugStringA("WM_DESTROY\n");

            return Result;
        }
        case WM_CLOSE:
        {
            GlobalRunning = false;
            OutputDebugStringA("WM_CLOSE\n");

            return Result;
        }
        case WM_ACTIVATEAPP:
        {
            OutputDebugStringA("WM_ACTIVATEAPP\n");

            return Result;
        }
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            uint32 VKCode = WParam;
            uint32 lp = LParam;

            WORD keyFlags = HIWORD( LParam );
            WORD scanCode = LOBYTE( keyFlags );

            BOOL isExtendedKey = (keyFlags & KF_EXTENDED) == KF_EXTENDED; // extended-key flag, 1 if scancode has 0xE0 prefix

            if ( isExtendedKey )
                scanCode = MAKEWORD( scanCode, 0xE0 );

            bool32 WasDown = ((LParam & (1 << 30)) != 0);
            bool32 IsDown  = ((LParam & (1 << 31)) == 0);

            if ( WasDown != IsDown )
            {
                if ( VKCode == 'W' )
                {
                }
                else if ( VKCode == 'A' )
                {
                }
                else if ( VKCode == 'S' )
                {
                }
                else if ( VKCode == 'D' )
                {
                }
                else if ( VKCode == 'Q' )
                {
                }
                else if ( VKCode == 'E' )
                {
                }
                else if ( VKCode == VK_UP )
                {
                }
                else if ( VKCode == VK_DOWN )
                {
                }
                else if ( VKCode == VK_LEFT )
                {
                }
                else if ( VKCode == VK_RIGHT )
                {
                }
                else if ( VKCode == VK_SPACE )
                {
                }
                else if ( VKCode == VK_ESCAPE )
                {
                    GlobalRunning = false;
                }
            }

            bool32 AltKeyWasDown = (LParam & (1 << 29)) != 0;
            if ( VKCode == VK_F4 && AltKeyWasDown )
            {
                GlobalRunning = false;
            }

            return 0;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);
                win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                Win32DisplayBufferInWindow(DeviceContext, GlobalBackBuffer, Dimension.Width, Dimension.Height);
            EndPaint(Window, &Paint); // Will implicitly invalidate the region

            return Result;
        }

    }

    return DefWindowProcA( Window, Message, WParam, LParam );;
}

INTERNAL LRESULT CALLBACK Win32KeyboardCallback(int Code, WPARAM WParam, LPARAM LParam)
{
    // Code == HC_ACTION ; The wParam and lParam contain info about a keystroke message
    // Code == HC_NOREMOVE ; w/lParam contain info about a keystroke message, and the message has not been removed from the queue
    // WParam == virtual key code of the key that generated the keystroke message
    // LParam == The repeat count, scan code, extended-key flag, context code, previous key-state flag and transition-state flag.

    return 0;
    //return CallNextHookEx(Code, WParam, LParam);
}

//===============================================================
// Main windows entry point
//===============================================================
int WinMain( HINSTANCE Instance, HINSTANCE PrevWindow, LPSTR CmdLine, int ShowCmd)
{
    LARGE_INTEGER PerfCountFrequencyResult;
    QueryPerformanceFrequency( &PerfCountFrequencyResult );
    int64 PerfCountFrequency = PerfCountFrequencyResult.QuadPart;

    WNDCLASSA WindowClass = {};
    WindowClass.style = CS_HREDRAW|CS_VREDRAW|CS_OWNDC;
    WindowClass.lpfnWndProc = Win32WindowCallback;
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = "GFSWindowClass";

    Win32ResizeDIBSection(&GlobalBackBuffer, 1280, 720);

    if(RegisterClassA(&WindowClass))
    {
        LoadXInput();

        HWND Window = CreateWindowExA( 0, WindowClass.lpszClassName, "Game From Scratch", 
            WS_OVERLAPPEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 
            0, 0, Instance, 0 );

        if(Window)
        {
            // NOTE(oyvind): Since we specified CS_OWNDC, we can just
            // get one device context and use it forever, because we
            // are not sharing it with anyone.
            HDC DeviceContext = GetDC( Window );

            // NOTE(oyvind): Debug bs for rendering pixels
            int XOffset = 0;
            int YOffset = 0;

            win32_sound_output SoundOutput = {};
            SoundOutput.SamplesPerSecond = 48000;
            SoundOutput.ToneHz = 144;
            SoundOutput.ToneVolume = 500;
            SoundOutput.RunningSampleIndex = 0;
            SoundOutput.WavePeriod = SoundOutput.SamplesPerSecond / SoundOutput.ToneHz;
            SoundOutput.BytesPerSample = sizeof( int16 ) * 2;
            SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;
            SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSecond / 15;

            Win32InitSound(Window, SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize );
            Win32ClearBuffer( &SoundOutput );
            GlobalSecondaryBuffer->Play( 0, 0, DSBPLAY_LOOPING );

            GlobalRunning = true;

            int16* Samples = (int16*)VirtualAlloc( 0, SoundOutput.SecondaryBufferSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

            LARGE_INTEGER LastCounter;
            QueryPerformanceCounter( &LastCounter );
            uint64 LastCycleCount = __rdtsc();
            while(GlobalRunning)
            {
                //-------------------------------------------------------------------------------------------------
                // Handle windows messages
                //-------------------------------------------------------------------------------------------------
                MSG Message;
                while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
                {
                    if(Message.message == WM_QUIT)
                    {
                        GlobalRunning = false;
                    }
                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                }

                //-------------------------------------------------------------------------------------------------
                // XInput handling
                // TODO(oyvind): Should we poll more frequently than pr frame?
                //-------------------------------------------------------------------------------------------------
                for (DWORD ControllerIndex = 0; ControllerIndex < XUSER_MAX_COUNT; ControllerIndex++ )
                {
                    XINPUT_STATE ControllerState;
                    if ( XInputGetState( ControllerIndex, &ControllerState) == ERROR_SUCCESS )
                    {
                        // NOTE(oyvind): Controller is connected
                        // NOTE(oyvind): See if ControllerState.dwPacketNumber increments too rapidly
                        XINPUT_GAMEPAD* Pad = &ControllerState.Gamepad;

                        bool32 Up               = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                        bool32 Down             = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                        bool32 Left             = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                        bool32 Right            = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                        bool32 Start            = (Pad->wButtons & XINPUT_GAMEPAD_START);
                        bool32 Back             = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                        bool32 LeftShoulder     = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                        bool32 RightShoulder    = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                        bool32 AButton          = (Pad->wButtons & XINPUT_GAMEPAD_A);
                        bool32 BButton          = (Pad->wButtons & XINPUT_GAMEPAD_B);
                        bool32 XButton          = (Pad->wButtons & XINPUT_GAMEPAD_X);
                        bool32 YButton          = (Pad->wButtons & XINPUT_GAMEPAD_Y);

                        int16 StickLX           = Pad->sThumbLX;
                        int16 StickLY           = Pad->sThumbLY;
                        int16 StickRX           = Pad->sThumbRX;
                        int16 StickRY           = Pad->sThumbRY;

                        // TODO(oyvind): Debug purposes. Deleteme
                        SoundOutput.ToneHz = 512 + (int)(256.0f * (StickRY / 30000.0f));
                        SoundOutput.WavePeriod = SoundOutput.SamplesPerSecond / SoundOutput.ToneHz;

                        XOffset = StickRX / XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
                        YOffset = StickRY / XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;

                        // TODO(oyvind): Debug purposes. Deleteme
                        char LogBuffer[256];
                        sprintf( LogBuffer, "X: %d, Y: %d\n", XOffset, YOffset );
                        OutputDebugStringA( LogBuffer );
                    }
                    else
                    {
                        // NOTE(oyvind): Controller is not connected
                    }
                }

                //-------------------------------------------------------------------------------------------------
                // Rendering and audio
                // NOTE(oyvind): Render screen and audio
                //-------------------------------------------------------------------------------------------------
                
                DWORD BytesToLock;
                DWORD TargetCursor;
                DWORD BytesToWrite;
                DWORD PlayCursor;
                DWORD WriteCursor;
                bool32 SoundIsValid = false;

                // NOTE(oyvind): Figure out how much we should write to the ringbuffer
                if ( SUCCEEDED( GlobalSecondaryBuffer->GetCurrentPosition( &PlayCursor, &WriteCursor ) ) )
                {
                    BytesToLock = (SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) % SoundOutput.SecondaryBufferSize;
                    TargetCursor = (PlayCursor + (SoundOutput.LatencySampleCount * SoundOutput.BytesPerSample)) % SoundOutput.SecondaryBufferSize;

                    if ( BytesToLock > TargetCursor )
                    {
                        BytesToWrite = SoundOutput.SecondaryBufferSize - BytesToLock;
                        BytesToWrite += TargetCursor;
                    }
                    else
                    {
                        BytesToWrite = TargetCursor - BytesToLock;
                    }

                    SoundIsValid = true;
                }

                gfs_sound_buffer SoundBuffer = {};
                SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;;
                SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
                SoundBuffer.Samples = Samples;

                gfs_offscreen_buffer buffer = {};
                buffer.Memory = GlobalBackBuffer.Memory;
                buffer.Width = GlobalBackBuffer.Width;
                buffer.Height = GlobalBackBuffer.Height;
                buffer.Pitch = GlobalBackBuffer.Pitch;

                GameUpdateAndRender(&buffer, XOffset, YOffset, &SoundBuffer);

                //-------------------------------------------------------------------------------------------------
                // NOTE(oyvind): DXsound output test
                //-------------------------------------------------------------------------------------------------
                if(SoundIsValid )
                {
                    Win32FillSoundBuffer( &SoundOutput, BytesToLock, BytesToWrite, &SoundBuffer );
                }

                win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                Win32DisplayBufferInWindow(DeviceContext, GlobalBackBuffer, Dimension.Width, Dimension.Height);


                //-------------------------------------------------------------------------------------------------
                // NOTE(oyvind): Timings
                //-------------------------------------------------------------------------------------------------
                int64 EndCycleCount = __rdtsc();

                LARGE_INTEGER EndCounter;
                QueryPerformanceCounter( &EndCounter );

                int64 CyclesElapsed = EndCycleCount - LastCycleCount;
                int64 CounterElapsed = EndCounter.QuadPart - LastCounter.QuadPart;

                real32 MSPerFrame = ((1000 * (real32)CounterElapsed) / (real32)PerfCountFrequency);
                real32 FPS = (real32)PerfCountFrequency / (real32)CounterElapsed;
                real32 MegaCyclesPerFrame = (real32)CyclesElapsed / (1000 * 1000);

                
                // TODO(oyvind): Implement platform independent logging
                //char LogBuffer[256];
                //sprintf( LogBuffer, "%.03fms/f | %.02ff/s | %.02fmcy/f\n", MSPerFrame, FPS, MegaCyclesPerFrame );
                //OutputDebugStringA( LogBuffer );

                LastCycleCount = EndCycleCount;
                LastCounter = EndCounter;
            }
        }
        else
        {
            // TODO(oyvind): Logging
        }
    }
    else
    {
        // TODO(oyvind): Logging
    }

    return 0;
}