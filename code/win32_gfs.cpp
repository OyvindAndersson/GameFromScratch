
#include <windows.h>
#include <stdint.h>

//----------------------------------------------
// Doc
//----------------------------------------------

#define INTERNAL static
#define GLOBALVAR static
#define LOCALPERSIST static

GLOBALVAR bool Running;

struct win32_offscreen_buffer{
    BITMAPINFO Info;
    void *Memory;
    int Width;
    int Height;
    int BytesPerPixel;
    int Pitch;
};

GLOBALVAR win32_offscreen_buffer GlobalBackBuffer;

struct win32_window_dimension
{
    int Width;
    int Height;
};

//----------------------------------------------
// Doc
//----------------------------------------------
INTERNAL win32_window_dimension Win32GetWindowDimension(HWND Window)
{
    win32_window_dimension Result;
    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    Result.Width = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return Result;
}

//----------------------------------------------
// Doc
//----------------------------------------------
INTERNAL void RenderWeirdPixelTest(win32_offscreen_buffer Buffer, int XOffset, int YOffset)
{
    
    uint8_t *Row = (uint8_t *)Buffer.Memory;

    for(int Y = 0; Y < Buffer.Height; ++Y)
    {
        uint32_t *Pixel = (uint32_t *)Row;
        for( int X = 0; X < Buffer.Width; ++X)
        {
            uint8_t Red = (X + XOffset);
            uint8_t Blue = (Y + YOffset);

            *Pixel++ = ((Red << 16) | (0 << 8) | (Blue));
        }
        Row += Buffer.Pitch;
    }
}

//----------------------------------------------
// Doc
//----------------------------------------------
INTERNAL void Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
    if(Buffer->Memory)
    {
        VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
    }

    Buffer->Width = Width;
    Buffer->Height = Height;
    Buffer->BytesPerPixel = 4;

    Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
    Buffer->Info.bmiHeader.biWidth = Buffer->Width;
    Buffer->Info.bmiHeader.biHeight = -Buffer->Height; // Neg. sign to create top-down image
    Buffer->Info.bmiHeader.biPlanes = 1;
    Buffer->Info.bmiHeader.biBitCount = 32;
    Buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BitmapImageMemorySize = Buffer->Width * Buffer->Height * Buffer->BytesPerPixel;
    Buffer->Memory = VirtualAlloc(0, BitmapImageMemorySize, MEM_COMMIT, PAGE_READWRITE);
    Buffer->Pitch = Buffer->Width * Buffer->BytesPerPixel;

    // TODO(oyvind): Clear to black?
}

//----------------------------------------------
// Doc
//----------------------------------------------
INTERNAL void Win32DisplayBufferInWindow(HDC DeviceContext, win32_offscreen_buffer Buffer, int WindowWidth, int WindowHeight)
{
    StretchDIBits(DeviceContext,
        0, 0, WindowWidth, WindowHeight,   // Dest
        0, 0, Buffer.Width, Buffer.Height, // Src
        Buffer.Memory,
        &Buffer.Info,
        DIB_RGB_COLORS, SRCCOPY );
}

//----------------------------------------------
// Doc
//----------------------------------------------
LRESULT CALLBACK Win32WindowCallback(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;
    switch(Message)
    {
        case WM_SIZE:
        {
        } break;

        case WM_DESTROY:
        {
            // TODO Handle as error, recreate window?
            Running = false;
            OutputDebugStringA("WM_DESTROY\n");
        } break;

        case WM_CLOSE:
        {
            Running = false;
            OutputDebugStringA("WM_CLOSE\n");
        } break;
        case WM_ACTIVATEAPP:
        {
            OutputDebugStringA("WM_ACTIVATEAPP\n");
        } break;
        case WM_PAINT:
        {
            PAINTSTRUCT Paint;
            HDC DeviceContext = BeginPaint(Window, &Paint);

            /* TODO NOT USED (YET). For redrawing only parts in backbuffer later? */
            int X = Paint.rcPaint.left;
            int Y = Paint.rcPaint.top;
            int Width = Paint.rcPaint.right - Paint.rcPaint.left;
            int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;

            win32_window_dimension Dimension = Win32GetWindowDimension(Window);
            Win32DisplayBufferInWindow(DeviceContext, GlobalBackBuffer, Dimension.Width, Dimension.Height /*, X, Y, Width, Height*/);

            EndPaint(Window, &Paint);
        }break;
        default:
        {
            Result = DefWindowProc(Window, Message, WParam, LParam);
        } break;
    }

    return Result;
}

//----------------------------------------------
// Main windows entry point
//----------------------------------------------
int WinMain( HINSTANCE Instance, HINSTANCE PrevWindow, PSTR CmdLine, int ShowCmd)
{
    WNDCLASS WindowClass = {};
    WindowClass.style = CS_HREDRAW|CS_VREDRAW;
    WindowClass.lpfnWndProc = Win32WindowCallback;
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = "GFSWindowClass";

    Win32ResizeDIBSection(&GlobalBackBuffer, 1280, 720);

    if(RegisterClassA(&WindowClass))
    {
        HWND Window = CreateWindowExA(0,
            WindowClass.lpszClassName,
            "Game From Scratch",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            0, 0, Instance, 0 
        );

        if(Window)
        {
            Running = true;
            int XOffset = 0;
            int YOffset = 0;

            while(Running)
            {
                MSG Message;
            
                while(PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
                {
                    if(Message.message == WM_QUIT)
                    {
                        Running = false;
                    }
                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                }

                RenderWeirdPixelTest(GlobalBackBuffer, XOffset, YOffset);

                HDC DeviceContext = GetDC(Window);
                win32_window_dimension Dimension = Win32GetWindowDimension(Window);
                Win32DisplayBufferInWindow(DeviceContext, GlobalBackBuffer, Dimension.Width, Dimension.Height);
                ReleaseDC(Window, DeviceContext);

                ++XOffset;

            }
        }
        else
        {
            // TODO Logging
        }
    }
    else
    {
        // TODO Logging
    }

    return 0;
}