////////////////////////////////
//~ brt: Helpers

internal FP_CT_Font
fp_ct_font_from_handle(FP_Handle handle)
{
  FP_CT_Font result = {0};
  result.cg_font = (CGFontRef)handle.u64[0];
  result.ct_font = (CTFontRef)handle.u64[1];
  return result;
}

internal FP_Handle
fp_ct_handle_from_font(FP_CT_Font font)
{
  FP_Handle result = {0};
  result.u64[0] = (U64)font.cg_font;
  result.u64[1] = (U64)font.ct_font;
  return result;
}

internal void
fp_ct_release_data_provider_info(void *info, const void *data, size_t size)
{
}

internal CTFontRef
fp_ct_font_make_sized(CGFontRef cg_font, F32 size)
{
  // NOTE(yuraiz): That makes the size consistent with the FreeType backend.
  size *= 96.0 / 72.0;
  CTFontRef result = 0;
  if (cg_font != 0)
  {
    result = CTFontCreateWithGraphicsFont(cg_font, size, 0, 0);
  }
  return result;
}

internal U16
fp_ct_glyph_index_from_codepoint(CTFontRef font, U32 codepoint)
{
  U16 result = 0;
  UniChar chars[2] = {0};
  CGGlyph glyphs[2] = {0};
  CFIndex char_count = 0;

  if (codepoint <= 0xFFFF)
  {
    chars[0] = (UniChar)codepoint;
    char_count = 1;
  }
  else
  {
    U32 cp = codepoint - 0x10000;
    chars[0] = (UniChar)(0xD800 + ((cp >> 10) & 0x3FF));
    chars[1] = (UniChar)(0xDC00 + (cp & 0x3FF));
    char_count = 2;
  }

  if (font != 0)
  {
    CTFontGetGlyphsForCharacters(font, chars, glyphs, char_count);
    result = (U16)glyphs[0];
  }

  return result;
}

////////////////////////////////
//~ brt: Backend Implementations

fp_hook void
fp_init(void)
{
  ProfBeginFunction();
  Arena *arena = arena_alloc();
  fp_ct_state = push_array(arena, FP_CT_State, 1);
  fp_ct_state->arena = arena;
  ProfEnd();
}

fp_hook FP_Handle
fp_font_open(String8 path)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);

  FP_CT_Font font = {0};

  //- brt: build initial path task
  typedef struct PathTask PathTask;
  struct PathTask
  {
    PathTask *next;
    String8 path;
  };
  PathTask start_task = {0, path};
  PathTask *first_task = &start_task;
  PathTask *last_task = first_task;

  //- brt: try to open font
  for (PathTask *t = first_task; t != 0 && font.cg_font == 0; t = t->next)
  {
    B32 file_exists = (os_properties_from_file_path(t->path).created != 0);
    String8 path = str8_copy(scratch.arena, t->path);
    if (file_exists)
    {
      CFStringRef path_cf = CFStringCreateWithCString(kCFAllocatorDefault, (char *)path.str, kCFStringEncodingUTF8);
      CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path_cf, kCFURLPOSIXPathStyle, false);
      CGDataProviderRef provider = CGDataProviderCreateWithURL(url);
      if (provider != 0)
      {
        font.cg_font = CGFontCreateWithDataProvider(provider);
        CGDataProviderRelease(provider);
      }
      CFRelease(url);
      CFRelease(path_cf);
    }
    // brt: failure trying just the normal path? -> generate new tasks that search in system folders
    if (t == first_task && font.cg_font == 0 && t->path.size != 0)
    {
      {
        PathTask *task = push_array(scratch.arena, PathTask, 1);
        task->path = str8f(scratch.arena, "/Library/Fonts/%S", path);
        SLLQueuePush(first_task, last_task, task);
      }
      {
        PathTask *task = push_array(scratch.arena, PathTask, 1);
        task->path = str8f(scratch.arena, "/System/Library/Fonts/%S", path);
        SLLQueuePush(first_task, last_task, task);
      }
    }
  }
  
  if (font.cg_font != 0)
  {
    font.ct_font = CTFontCreateWithGraphicsFont(font.cg_font, 12.f, 0, 0);
  }

  //- brt: handlify & return
  FP_Handle handle = {0};
  if (font.cg_font != 0 && font.ct_font != 0)
  {
    handle = fp_ct_handle_from_font(font);
  }
  scratch_end(scratch);
  ProfEnd();
  return handle;
}

fp_hook FP_Handle
fp_font_open_from_static_data_string(String8 *data_ptr)
{
  ProfBeginFunction();

  FP_CT_Font font = {0};
  if (data_ptr != 0 && data_ptr->str != 0 && data_ptr->size != 0)
  {
    CGDataProviderRef provider = CGDataProviderCreateWithData(data_ptr, data_ptr->str, data_ptr->size, fp_ct_release_data_provider_info);
    if (provider != 0)
    {
      font.cg_font = CGFontCreateWithDataProvider(provider);
      CGDataProviderRelease(provider);
    }
    if (font.cg_font != 0)
    {
      font.ct_font = CTFontCreateWithGraphicsFont(font.cg_font, 12.f, 0, 0);
    }
  }
  FP_Handle handle = fp_ct_handle_from_font(font);
  ProfEnd();
  return handle;
}

fp_hook void
fp_font_close(FP_Handle handle)
{
  ProfBeginFunction();
  FP_CT_Font font = fp_ct_font_from_handle(handle);
  if (font.ct_font != 0)
  {
    CFRelease(font.ct_font);
  }
  if (font.cg_font != 0)
  {
    CFRelease(font.cg_font);
  }
  ProfEnd();
}

fp_hook FP_Metrics
fp_metrics_from_font(FP_Handle handle)
{
  ProfBeginFunction();

  FP_CT_Font font = fp_ct_font_from_handle(handle);
  FP_Metrics result = {0};

  if (font.ct_font != 0)
  {
    int units_per_em = 0;
    if (font.cg_font != 0)
    {
      units_per_em = CGFontGetUnitsPerEm(font.cg_font);
    }

    result.design_units_per_em = (F32)units_per_em;
    result.ascent              = (F32)CGFontGetAscent(font.cg_font);
    result.descent             = (F32)(-CGFontGetDescent(font.cg_font));
    result.line_gap            = (F32)CGFontGetLeading(font.cg_font);

    if (units_per_em > 0)
    {
      CTFontRef units_font = CTFontCreateWithGraphicsFont(font.cg_font, (CGFloat)units_per_em, 0, 0);
      if (units_font != 0)
      {
        result.capital_height = (F32)CTFontGetCapHeight(units_font);
        CFRelease(units_font);
      }
    }
  }

  ProfEnd();
  return result;
}

fp_hook ASAN_NO_ADDR FP_RasterResult
fp_raster(Arena *arena, FP_Handle font_handle, F32 size, FP_RasterFlags flags, String8 string)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(&arena, 1);

  FP_RasterResult result = {0};
  FP_CT_Font font = fp_ct_font_from_handle(font_handle);

  if (font.cg_font == 0 || string.size == 0)
  {
    scratch_end(scratch);
    ProfEnd();
    return result;
  }

  CTFontRef sized_font = fp_ct_font_make_sized(font.cg_font, size);
  if (sized_font == 0)
  {
    scratch_end(scratch);
    ProfEnd();
    return result;
  }

  String32 string32 = str32_from_8(scratch.arena, string);
  U64 glyph_count = string32.size;

  CGGlyph *glyphs = push_array_no_zero(scratch.arena, CGGlyph, glyph_count);
  CGSize  *advances = push_array_no_zero(scratch.arena, CGSize, glyph_count);
  CGRect  *bounds = push_array_no_zero(scratch.arena, CGRect, glyph_count);

  F32 total_advance = 0.f;
  F32 min_x = 0.f;
  F32 max_x = 0.f;
  F32 ascent = (F32)ceil(CTFontGetAscent(sized_font));
  F32 descent = (F32)ceil(CTFontGetDescent(sized_font));
  F32 leading = (F32)ceil(CTFontGetLeading(sized_font));

  {
    F32 pen_x = 0.f;
    for (U64 idx = 0; idx < glyph_count; idx += 1)
    {
      glyphs[idx] = (CGGlyph)fp_ct_glyph_index_from_codepoint(sized_font, string32.str[idx]);

      CTFontGetAdvancesForGlyphs(sized_font, kCTFontOrientationHorizontal, &glyphs[idx], &advances[idx], 1);
      bounds[idx] = CTFontGetBoundingRectsForGlyphs(sized_font, kCTFontOrientationHorizontal, &glyphs[idx], 0, 1);

      F32 gx0 = pen_x + (F32)bounds[idx].origin.x;
      F32 gx1 = gx0 + (F32)bounds[idx].size.width;

      if(idx == 0 || gx0 < min_x) { min_x = gx0; }
      if(idx == 0 || gx1 > max_x) { max_x = gx1; }

      pen_x += (F32)advances[idx].width;
    }

    total_advance = pen_x;
  }

  S16 atlas_w = (S16)Max(0, (S32)ceil_f32(Max(max_x, total_advance) - Min(0.f, min_x) + 2.f));
  S16 atlas_h = (S16)Max(0, (S32)ceil_f32(ascent + descent + leading + 1.f));

  atlas_w += 7;
  atlas_w -= atlas_w % 8;

  if (atlas_w <= 0 || atlas_h <= 0)
  {
    CFRelease(sized_font);
    scratch_end(scratch);
    ProfEnd();
    return result;
  }

  size_t bytes_per_row = (size_t)atlas_w * 4;
  U8 *rgba = push_array_no_zero(arena, U8, (U64)bytes_per_row * (U64)atlas_h);
  MemoryZero(rgba, (U64)bytes_per_row * (U64)atlas_h);

  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(rgba,
                                           (size_t)atlas_w,
                                           (size_t)atlas_h,
                                           8,
                                           bytes_per_row,
                                           color_space,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(color_space);

  if (ctx != 0)
  {
    CGContextClearRect(ctx, CGRectMake(0, 0, atlas_w, atlas_h));

    CGContextSetRGBFillColor(ctx, 1.f, 1.f, 1.f, 1.f);

    F32 baseline_top_down = ascent;
    F32 baseline_y = (F32)atlas_h - baseline_top_down;
    F32 pen_x = -Min(0.f, min_x) + 1.f;

    for (U64 idx = 0; idx < glyph_count; idx += 1)
    {
      CGPoint pos = CGPointMake((CGFloat)pen_x, (CGFloat)baseline_y);
      CTFontDrawGlyphs(sized_font, &glyphs[idx], &pos, 1, ctx);
      pen_x += (F32)advances[idx].width;
    } 

    CGContextFlush(ctx);

    result.atlas_dim = v2s16(atlas_w, atlas_h);
    result.atlas = rgba;
    result.advance = floor_f32(total_advance);

    {
      U64 color_sum = 0;
      for (S32 y = 0; y < atlas_h; y += 1)
      {
        U8 *row = rgba + (U64)y * bytes_per_row;
        for (S32 x = 0; x < atlas_w; x += 1)
        {
          U8 *px = row + x*4;
          U8 a = px[3];
          px[0] = 255;
          px[1] = 255;
          px[2] = 255;
          px[3] = a;
          color_sum += a;
        }
      }

      if (color_sum == 0)
      {
        result.atlas_dim = v2s16(0, 0);
      }
    }

    CGContextRelease(ctx);
  }

  CFRelease(sized_font);
  scratch_end(scratch);
  ProfEnd();
  return result;
}
