// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#include "font_provider.c"

#if FP_BACKEND == FP_BACKEND_DWRITE
# include "dwrite/font_provider_dwrite.c"
#elif FP_BACKEND == FP_BACKEND_FREETYPE
# include "freetype/font_provider_freetype.c"
#elif FP_BACKEND == FP_BACKEND_CORE_TEXT
# include "core_text/font_provider_core_text.c"
#else
# error Font provider backend not specified.
#endif
