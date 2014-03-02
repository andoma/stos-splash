#include <sys/param.h>
#include <stdint.h>

#include "text.h"

#include <ft2build.h>  
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H
#include FT_STROKER_H

static FT_Library text_library;
static FT_Stroker text_stroker;
static FT_Face text_face;

/**
 *
 */
void
text_init(const char *font)
{
  int error = FT_Init_FreeType(&text_library);
  if(error) {
    fprintf(stderr, "Freetype error %d\n", error);
    exit(1);
  }
  FT_Stroker_New(text_library, &text_stroker);

  error = FT_New_Face(text_library, font, 0, &text_face);
  if(error) {
    fprintf(stderr, "Unable to load font %s -- %d\n", font, error);
    exit(1);
  }
}


/**
 *
 */
static void
draw_glyph(uint8_t *dst, int width, int height, 
	   int left, int top, FT_Bitmap *bmp)
{

  for(int y = 0; y < bmp->rows; y++) {
    int row = top + y;
    if(row < 0 || row >= height)
      continue;
    uint8_t *r = dst + row * width;

    const uint8_t *src = bmp->buffer + bmp->width * y;

    for(int x = 0; x < bmp->width; x++) {
      int p = x + left;
      if(p < 0 || p >= width)
	continue;
      r[p] = MIN(src[x] + r[p], 255);
    }
  }
}



/**
 *
 */
static void *
text_render_int(const int *ucvec, int len, int size,
		int *widthp, int *heightp)
{
  int err;
  FT_Glyph glyphs[len];
  FT_BBox bbox[len];
  int advx[len];
  int kerning[len];
  FT_GlyphSlot gs;
  int prev_gi = 0;
  FT_Vector delta;

  FT_Size_RequestRec  req;
  req.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
  req.width = size << 6;
  req.height = size << 6;
  req.horiResolution = 0;
  req.vertResolution = 0;
  FT_Request_Size(text_face, &req);

  int width = 0;
  int height = size;


  int descender = 64 * text_face->descender * size / text_face->units_per_EM;

  for(int i = 0; i < len; i++) {
    FT_UInt gi = FT_Get_Char_Index(text_face, ucvec[i]);

    if((err = FT_Load_Glyph(text_face, gi, FT_LOAD_FORCE_AUTOHINT)) != 0)
      return NULL;

    gs = text_face->glyph;

    if((err = FT_Get_Glyph(gs, &glyphs[i])) != 0) {
      return NULL;
    }

    FT_Glyph_Get_CBox(glyphs[i], FT_GLYPH_BBOX_GRIDFIT, &bbox[i]);

    advx[i] = gs->advance.x;
    if(prev_gi) {
      err = FT_Get_Kerning(text_face, prev_gi, gi, FT_KERNING_DEFAULT, &delta);
      kerning[i] = delta.x;
    } else {
      kerning[i] = 0;
    }

    prev_gi = gi;
    width += gs->advance.x;
  }


  width >>= 6;

  uint8_t *bitmap = calloc(width, height);

  *widthp = width;
  *heightp = height;
  

#if 0
  for(int y = 0; y < height; y++) {
    for(int x = 0; x < width; x++) {
      bitmap[width * y + x] = x & 1 ? 0x20 : 00;
    }
  }
#endif

  int target_height = size;

  FT_Vector pen;
  pen.x = 0;
  pen.y = descender;

  for(int i = 0; i < len; i++) {

    if(FT_Glyph_To_Bitmap(&glyphs[i], FT_RENDER_MODE_NORMAL, NULL, 0)) {
      glyphs[i] = NULL;
      continue;
    }

    FT_BitmapGlyph bg = (FT_BitmapGlyph)glyphs[i];
    FT_Bitmap *bmp = &bg->bitmap;

    draw_glyph(bitmap, width, height,
	       bg->left + (pen.x >> 6),
	       target_height - bg->top + (pen.y >> 6),
	       bmp);
    pen.x += advx[i];

    FT_Done_Glyph(glyphs[i]);
  }
  return bitmap;
}


/**
 *
 */
void *
text_render(const char *str, int len, int *widthp, int *heightp)
{
  int *uc = alloca(len * sizeof(int));

  for(int i = 0; i < len; i++)
    uc[i] = str[i];

  return text_render_int(uc, len, 40, widthp, heightp);
}
