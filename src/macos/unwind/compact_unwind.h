// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef COMPACT_UNWIND_H
#define COMPACT_UNWIND_H

#include <mach-o/compact_unwind_encoding.h>

////////////////////////////////
//~ rjf: Module Unwind Info Type

typedef struct COMP_UWND_ModuleUnwindInfo COMP_UWND_ModuleUnwindInfo;
struct COMP_UWND_ModuleUnwindInfo
{
  String8 data;
};

////////////////////////////////
//~ rjf: Unwinding Abstraction Implementation

internal UWND_StepResult compact_uwnd_step(Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *regs, U64 *cfa_out);

#endif // COMPACT_UNWIND_H
