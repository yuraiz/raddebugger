#ifndef FONT_PROVIDER_CORE_TEXT_H
#define FONT_PROVIDER_CORE_TEXT_H

////////////////////////////////
//~ brt: CoreText Includes

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

////////////////////////////////
//~ brt: State Types

typedef struct FP_CT_State FP_CT_State;
struct FP_CT_State
{
  Arena *arena;
};

typedef struct FP_CT_Font FP_CT_Font;
struct FP_CT_Font
{
  CGFontRef cg_font;
  CTFontRef ct_font;
};

////////////////////////////////
//~ brt: Globals

global FP_CT_State *fp_ct_state = 0;

////////////////////////////////
//~ brt: Helpers

internal FP_CT_Font fp_ct_font_from_handle(FP_Handle handle);
internal FP_Handle fp_ct_handle_from_font(FP_CT_Font font);

#endif // FONT_PROVIDER_CORE_TEXT_H
