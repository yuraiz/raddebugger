// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MACH_O_H
#define MACH_O_H

////////////////////////////////

// CF FA ED FE
read_only global U8 mach_magic[] = {0xCF, 0xFA, 0xED, 0xFE};
read_only global String8 mach_magic_string = {mach_magic, sizeof(mach_magic)};

typedef struct MACH_Bin MACH_Bin;
struct MACH_Bin
{
  U32 command_count;
  U8 *buf;
  U64 size;
  String8 dwarf_info;
};

typedef B32 (*MACH_ReadOp)(U64 address, U64 size, void* dst, U64 src);

////////////////////////////////

internal B32 dmn_mac_mach_read_op_file(U64 address, U64 size, void* dst, U64 src);

internal MACH_Bin mach_bin_read_from_file(Arena* arena, String8 bin_path);

internal MACH_Bin mach_read_extract_file_info(Arena* arena, U64 start_address, MACH_ReadOp read_from, U64 src);
internal Guid mach_get_uuid(MACH_Bin info);
internal String8 mach_try_locate_dsym(Arena* arena, String8 image_file_path, Guid uuid);
internal void mach_dump_commands(MACH_Bin info);

#endif // MACH_O_H
