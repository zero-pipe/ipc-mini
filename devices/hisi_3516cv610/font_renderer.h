#ifndef FontRenderer_include_h
#define FontRenderer_include_h

#include "dev_std.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
#include <string>
//#include <glib.h>

typedef struct
{
	wchar_t c;
	int32_t font_size;
    FT_Face glyph_face;
    FT_Glyph glyph_ori;
    FT_Glyph glyph_outline;
    int32_t top;
    int32_t w;
}basic_char_t;

class FontRenderer
{
public:
	FontRenderer(const char* font_path);

	~FontRenderer();

	/** Must be called before init(); ignored after FreeType is loaded. */
	void set_font_path(const char* font_path);

	bool init();

	bool is_init();

	void release();

	bool get_width(const char* str,int32_t font_pixel,int* w);

    bool get_max_width(const char* str,int32_t font_pixel,int* w);

	bool show_string(const char* str,int32_t area_w,int32_t area_h,int32_t font_pixel,unsigned char* pdata,int32_t data_size,short bg_color,short fg_color,short outline_color);

	bool show_string_compare(const char* str_before,const char* str_now,int32_t area_w,int32_t area_h,int32_t font_pixel,unsigned char* pdata,int32_t data_size,short bg_clor,short fg_color,short outline_color);

private:
    bool get_glyph_char(wchar_t c,int32_t font_size,basic_char_t* char_info);
	bool get_glyph(FT_Face face,FT_ULong ch,FT_Glyph* org,FT_Glyph* outline);

	bool draw_rgb1555_char(int32_t area_w,
            int32_t area_h,
            int32_t font_pixel,
            FT_Glyph orig,
            FT_Glyph outline,
            int32_t startx,
            int32_t starty,
            unsigned char* buf,
            int32_t size,
            short bg_color,
            short fg_color,
            short outline_color);

	bool get_basic_char(wchar_t c,int32_t font_size,basic_char_t& char_info);

private:
	bool m_inited;
	std::string m_font_path;
	FT_Library  m_ft_lib;
	FT_Face		m_ft_face;
};

#endif

