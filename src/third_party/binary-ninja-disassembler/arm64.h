#ifndef BINARY_NINJA_ARM64_H
#define BINARY_NINJA_ARM64_H

#define disassemble bn_disassemble
#define UInt bn_UInt

#include "disassembler/arm64dis.h"

#ifdef BINARY_NINJA_ARM64_IMPLEMENTATION

#include "disassembler/decode_fields32.c"
#include "disassembler/decode_scratchpad.c"
#include "disassembler/decode.c"
#include "disassembler/decode0.c"
#include "disassembler/decode1.c"
#include "disassembler/decode2.c"
#include "disassembler/encodings_dec.c"
#include "disassembler/format.c"
#include "disassembler/operations.c"
#include "disassembler/pcode.c"
#include "disassembler/regs.c"
#include "disassembler/sysregs_fmt_gen.c"
#include "disassembler/sysregs_gen.c"

#endif // BINARY_NINJA_ARM64_IMPLEMENTATION

#undef disassemble
#undef UInt

#endif // BINARY_NINJA_ARM64_H

