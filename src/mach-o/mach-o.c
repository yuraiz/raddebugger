// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifdef OS_MAC

////////////////////////////////
//~ Includes

#undef internal 
#include <mach-o/dyld_images.h>
#include <mach-o/dyld.h>
#define internal static

internal B32
dmn_mac_mach_read_op_file(U64 address, U64 size, void* dst, U64 src)
{
  OS_Handle file = {src};
  return os_file_read(file, r1u64(address, address + size), dst);
}

internal MACH_Bin
mach_extract_file_info(Arena* arena, U64 start_address, MACH_ReadOp read_data, U64 src)
{
  MACH_Bin result = {0};
  
  struct mach_header_64 header;
  read_data(start_address, sizeof(header), &header, src);
  Assert(header.magic == MH_MAGIC_64 && "Invalid mach-o header magic");

  result.command_count = header.ncmds;

  start_address += sizeof(header);

  U8 *command_buf = push_array(arena, U8, header.sizeofcmds);

  if(read_data(start_address, header.sizeofcmds, command_buf, src))
  {
    result.buf = command_buf;
    result.size = header.sizeofcmds;
  }

  return result;
}

internal Guid
mach_get_uuid(MACH_Bin info)
{
  Guid result = {0};
  U8 *command_buf = info.buf;
  for EachIndex(i, info.command_count)
  {
    struct load_command *ld_cmd = (struct load_command *)command_buf;
    U32 cmd = ld_cmd->cmd;
    U32 size = ld_cmd->cmdsize;
    
    switch(cmd)
    {
    default:{break;}
    case LC_UUID:
    {
      struct uuid_command *command = (struct uuid_command*)ld_cmd;
      
      Guid guid;
      MemoryCopyArray(guid.v, command->uuid);
      guid.data1 = from_be_u32(guid.data1);
      guid.data2 = from_be_u16(guid.data2);
      guid.data3 = from_be_u16(guid.data3);

      return guid;
    } break;
    }
    command_buf += size;
  }
  return result;
}

internal B32
dw_is_dwarf_present_from_mach_bin(MACH_Bin info)
{
  B32 result = 0;
  U8 *command_buf = info.buf;
  for EachIndex(i, info.command_count)
  {
    struct load_command *ld_cmd = (struct load_command *)command_buf;
    U32 cmd = ld_cmd->cmd;
    U32 size = ld_cmd->cmdsize;
    
    switch(cmd)
    {
      default:{break;}
      case LC_SEGMENT_64:
      {
        struct segment_command_64 *command = (struct segment_command_64*)ld_cmd;
        if(str8_match(str8_cstring(command->segname), str8_lit("__DWARF"), 0))
        {
          result = 1;
          return result;
        }
      } break;
    }
    command_buf += size;
  }
  return result;
}

internal DW_Input
dw_input_from_mach_bin(Arena *arena, MACH_Bin bin, String8 data)
{
  DW_Input result = {0};
  U8 *command_buf = bin.buf;
  U64 total_sections = 0;
  for EachIndex(i, bin.command_count)
  {
    struct load_command *ld_cmd = (struct load_command *)command_buf;
    U32 cmd = ld_cmd->cmd;
    U32 size = ld_cmd->cmdsize;
    
    switch(cmd)
    {
      default:{break;}
      case LC_SEGMENT_64:
      {
        struct segment_command_64 *command = (struct segment_command_64*)ld_cmd;
        if(str8_match(str8_cstring(command->segname), str8_lit("__DWARF"), 0))
        {
          struct section_64 *sections = (struct section_64*)(command_buf + sizeof(struct segment_command_64));

          for EachIndex(i, command->nsects)
          {
            struct section_64 s = sections[i];
                
            String8 section_name = str8_cstring_capped(s.sectname, s.segname);
                    
            // String8 section_name = elf_name_from_shdr64(data, bin, shdr);
            DW_SectionKind section_kind = dw_section_kind_from_string_apple(section_name);

            String8 section_data = str8_substr(data, r1u64(s.offset, s.offset + s.size));

            if(section_kind == DW_Section_Null)
            {
              fprintf(stderr, "Unknown section kind: %d\n", section_kind);
            }
            
            DW_Section *d = &result.sec[section_kind];
            d->name   = push_str8_copy(arena, section_name);
            d->data   = section_data;
            // .dwo isn't supported on macos
            d->is_dwo = 0;
          }
        }
      } break;
    }
    command_buf += size;
  }

  return result;
}

internal U64
mach_compute_image_size(MACH_Bin info)
{
  U64 result = 0;
  U8 *command_buf = info.buf;
  for EachIndex(i, info.command_count)
  {
    struct load_command *ld_cmd = (struct load_command *)command_buf;
    U32 cmd = ld_cmd->cmd;
    U32 size = ld_cmd->cmdsize;
    switch(cmd)
    {
      default:{break;}
      case LC_SEGMENT:
      {
        struct segment_command *command = (struct segment_command*)ld_cmd;
        result += command->vmsize;
      } break;
      case LC_SEGMENT_64:
      {
        struct segment_command_64 *command = (struct segment_command_64*)ld_cmd;
        result += command->vmsize;
      } break;
    }
    command_buf += size;
  }
  return result;
}

// TODO(yuraiz): I think we actually interested only in the file location
internal MACH_Bin
mach_try_locate_dsym(Arena* arena, String8 image_file_path, Guid uuid)
{
  Temp scratch = scratch_begin(&arena, 1);

  String8 candidate = push_str8f(scratch.arena, "%S.dSYM", image_file_path);
  if(os_folder_path_exists(candidate))
  {
    String8 dwarf_folder = push_str8f(scratch.arena, "%S/Contents/Resources/DWARF", candidate);

    String8 bin_path = str8_zero();

    OS_FileIter *iter = os_file_iter_begin(scratch.arena, dwarf_folder, 0);
    for(OS_FileInfo info = {0}; os_file_iter_next(scratch.arena, iter, &info);)
    {
      if(!(info.props.flags & FilePropertyFlag_IsFolder))
      {
        if(bin_path.size == 0)
        {
          bin_path = push_str8_copy(scratch.arena, info.name);
        }
        else
        {
          Assert(0 && "Unexpected binary file count in .dSYM");
        }
      }
    }
    os_file_iter_end(iter);
    
    if(bin_path.size > 0)
    {
      bin_path = push_str8f(scratch.arena, "%S/%S", dwarf_folder, bin_path);
      
      OS_Handle file = os_file_open(OS_AccessFlag_Read|OS_AccessFlag_ShareRead, bin_path);
      MACH_Bin result = mach_extract_file_info(arena, 0, dmn_mac_mach_read_op_file, file.u64[0]);
      Guid debug_uuid = mach_get_uuid(result);
      if(MemoryMatch(&uuid, &debug_uuid, sizeof(Guid)))
      {
        return result;
      }
    }
  }

  // TODO(yuraiz): Search in other locations
  // Also try
  // mdfind 'com_apple_xcode_dsym_uuids=*'

  scratch_end(scratch);
  MACH_Bin result = {0};
  return result;
}

internal void
mach_dump_commands(MACH_Bin info)
{
  Temp scratch = scratch_begin(0, 0);

  U8 *command_buf = info.buf;
  for EachIndex(i, info.command_count)
  {
    struct load_command *ld_cmd = (struct load_command *)command_buf;
    U32 cmd = ld_cmd->cmd;
    U32 size = ld_cmd->cmdsize;
    
    switch(cmd)
    {
      default:
      {
        printf("unknown %d, size: %d\n", cmd, size);
      } break;
      case LC_UUID:
      {
        printf("uuid_command\n");
        struct uuid_command *command = (struct uuid_command*)ld_cmd;
        
        Guid guid;
        MemoryCopyArray(guid.v, command->uuid);
        guid.data1 = from_be_u32(guid.data1);
        guid.data2 = from_be_u16(guid.data2);
        guid.data3 = from_be_u16(guid.data3);

        printf(" uuid: %s\n", string_from_guid(scratch.arena, guid).str);
      } break;
      case LC_SEGMENT:
      {
        struct segment_command *command = (struct segment_command*)ld_cmd;
        printf("segment_command\n");
        printf("  name: %s\n", command->segname);
        printf("  vmaddr: %d\n", command->vmaddr);
        printf("  vmsize: %d\n", command->vmsize);
        printf("  fileoff: %d\n", command->fileoff);
        printf("  filesize: %d\n", command->filesize);
        printf("  maxprot: %d\n", command->maxprot);
        printf("  initprot: %d\n", command->initprot);
        printf("  nsects: %d\n", command->nsects);
        printf("  flags: %d\n", command->flags);
      } break;
      case LC_SEGMENT_64:
      {
        struct segment_command_64 *command = (struct segment_command_64*)ld_cmd;
        printf("segment_command_64\n");
        printf("  name: %s\n", command->segname);
        printf("  vmaddr: %d\n", command->vmaddr);
        printf("  vmsize: %d\n", command->vmsize);
        printf("  fileoff: %d\n", command->fileoff);
        printf("  filesize: %d\n", command->filesize);
        printf("  maxprot: %d\n", command->maxprot);
        printf("  initprot: %d\n", command->initprot);
        printf("  nsects: %d\n", command->nsects);
        printf("  flags: %d\n", command->flags);
      } break;
      case LC_SYMTAB:
      {
        struct symtab_command *command = (struct symtab_command*)ld_cmd;
        printf("symtab_command\n");
        printf("  symoff %d\n", command->symoff);
        printf("  nsyms %d\n", command->nsyms);
        printf("  stroff %d\n", command->stroff);
        printf("  strsize %d\n", command->strsize);
      } break;
      case LC_DYSYMTAB:
      {
        struct dysymtab_command *command = (struct dysymtab_command*)ld_cmd;
        printf("dysymtab_command\n");
        printf("  ilocalsym %d\n", command->ilocalsym);
        printf("  nlocalsym %d\n", command->nlocalsym);
        printf("  iextdefsym %d\n", command->iextdefsym);
        printf("  nextdefsym %d\n", command->nextdefsym);
        printf("  iundefsym %d\n", command->iundefsym);
        printf("  nundefsym %d\n", command->nundefsym);
        printf("  tocoff %d\n", command->tocoff);
        printf("  ntoc %d\n", command->ntoc);
        printf("  modtaboff %d\n", command->modtaboff);
        printf("  nmodtab %d\n", command->nmodtab);
        printf("  extrefsymoff %d\n", command->extrefsymoff);
        printf("  nextrefsyms %d\n", command->nextrefsyms);
        printf("  indirectsymoff %d\n", command->indirectsymoff);
        printf("  nindirectsyms %d\n", command->nindirectsyms);
        printf("  extreloff %d\n", command->extreloff);
        printf("  nextrel %d\n", command->nextrel);
        printf("  locreloff %d\n", command->locreloff);
        printf("  nlocrel %d\n", command->nlocrel);
      } break;
      case LC_THREAD:
      {
        printf("thread_command\n");
        struct thread_command *command = (struct thread_command*)ld_cmd;
        } break;
      case LC_UNIXTHREAD:
      {
        printf("thread_command\n");
        struct thread_command *command = (struct thread_command*)ld_cmd;
      } break;
      case LC_LOAD_DYLIB:
      {
        printf("dylib_command\n");
        struct dylib_command *command = (struct dylib_command*)ld_cmd;
      } break;
      case LC_ID_DYLIB:
      {
        printf("dylib_command\n");
        struct dylib_command *command = (struct dylib_command*)ld_cmd;
      } break;
      case LC_PREBOUND_DYLIB:
      {
        printf("prebound_dylib_command\n");
        struct prebound_dylib_command *command = (struct prebound_dylib_command*)ld_cmd;
      } break;
      case LC_LOAD_DYLINKER:
      {
        printf("dylinker_command\n");
        struct dylinker_command *command = (struct dylinker_command*)ld_cmd;
      } break;
      case LC_ID_DYLINKER:
      {
        printf("dylinker_command\n");
        struct dylinker_command *command = (struct dylinker_command*)ld_cmd;
      } break;
      case LC_ROUTINES:
      {
        printf("routines_command\n");
        struct routines_command *command = (struct routines_command*)ld_cmd;
      } break;
      case LC_ROUTINES_64:
      {
        printf("routines_command_64\n");
        struct routines_command_64 *command = (struct routines_command_64*)ld_cmd;
      } break;
      case LC_TWOLEVEL_HINTS:
      {
        printf("twolevel_hints_command\n");
        struct twolevel_hints_command *command = (struct twolevel_hints_command*)ld_cmd;
      } break;
      case LC_SUB_FRAMEWORK:
      {
        printf("sub_framework_command\n");
        struct sub_framework_command *command = (struct sub_framework_command*)ld_cmd;
      } break;
      case LC_SUB_UMBRELLA:
      {
        printf("sub_umbrella_command\n");
        struct sub_umbrella_command *command = (struct sub_umbrella_command*)ld_cmd;
      } break;
      case LC_SUB_LIBRARY:
      {
        printf("sub_library_command\n");
        struct sub_library_command *command = (struct sub_library_command*)ld_cmd;
      } break;
      case LC_SUB_CLIENT:
      {
        printf("sub_client_command\n");
        struct sub_client_command *command = (struct sub_client_command*)ld_cmd;
      } break;
    }
    
    command_buf += size;
  }

  scratch_end(scratch);
}

#else

////////////////////////////////
//~ Stubs

internal B32 dmn_mac_mach_read_op_file(U64 address, U64 size, void* dst, U64 src);

internal MACH_Bin 
mach_read_extract_file_info(Arena* arena, U64 start_address, MACH_ReadOp read_from, U64 src);
{
   MACH_Bin result = {0};
   return result;
}
internal Guid 
mach_get_uuid(MACH_Bin info);
{
   Guid result = {0};
   return result;
}
internal MACH_Bin 
mach_try_locate_dsym(Arena* arena, String8 image_file_path, Guid uuid);
{
   MACH_Bin result = {0};
   return result;
}
internal void 
mach_dump_commands(MACH_Bin info);
{
}

#endif // OS_MAC

internal MACH_Bin
mach_bin_read_from_file(Arena* arena, String8 bin_path)
{
  OS_Handle file = os_file_open(OS_AccessFlag_Read|OS_AccessFlag_ShareRead, bin_path);
  return mach_extract_file_info(arena, 0, dmn_mac_mach_read_op_file, file.u64[0]);
}