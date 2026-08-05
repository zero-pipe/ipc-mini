#include "dev_std.h"
#include "font_renderer.h"

const char* g_week_stsr[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
FontRenderer g_freetype("/opt/zero_mini/fonts/DejaVuSans.ttf");

int32_t rgb24to1555(int32_t r,int32_t g,int32_t b,int32_t a)
{
    int32_t r1555 = r & 0x1f;
    int32_t g1555 = g & 0x1f; 
    int32_t b1555 = b & 0x1f;

    if(a)
    {
        return  0x8000 | (r1555 << 10) | (g1555 << 5) | b1555;
    }
    else
    {
        return  (r1555 << 10) | (g1555 << 5) | b1555;
    }
}

