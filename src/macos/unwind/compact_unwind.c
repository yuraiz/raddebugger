// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal void
compact_uwnd_dump_encoding(compact_unwind_encoding_t encoding)
{
    U32 stack_size;
    switch (encoding & UNWIND_ARM64_MODE_MASK) {
      case UNWIND_ARM64_MODE_FRAMELESS:
      {
        stack_size = ((encoding & UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK) >> 12);
        if ( stack_size == 0 )
            printf("no frame, no saved registers ");
        else
            printf("stack size=%d: ", 16 * stack_size);
        if ( encoding & UNWIND_ARM64_FRAME_X19_X20_PAIR )
            printf("x19/20 ");
        if ( encoding & UNWIND_ARM64_FRAME_X21_X22_PAIR )
            printf("x21/22 ");
        if ( encoding & UNWIND_ARM64_FRAME_X23_X24_PAIR )
            printf("x23/24 ");
        if ( encoding & UNWIND_ARM64_FRAME_X25_X26_PAIR )
            printf("x25/26 ");
        if ( encoding & UNWIND_ARM64_FRAME_X27_X28_PAIR )
            printf("x27/28 ");
        if ( encoding & UNWIND_ARM64_FRAME_D8_D9_PAIR )
            printf("d8/9 ");
        if ( encoding & UNWIND_ARM64_FRAME_D10_D11_PAIR )
            printf("d10/11 ");
        if ( encoding & UNWIND_ARM64_FRAME_D12_D13_PAIR )
            printf("d12/13 ");
        if ( encoding & UNWIND_ARM64_FRAME_D14_D15_PAIR )
            printf("d14/15 ");
      }break;
      case UNWIND_ARM64_MODE_FRAME:
      {
        printf("std frame: ");
        if ( encoding & UNWIND_ARM64_FRAME_X19_X20_PAIR )
            printf("x19/20 ");
        if ( encoding & UNWIND_ARM64_FRAME_X21_X22_PAIR )
            printf("x21/22 ");
        if ( encoding & UNWIND_ARM64_FRAME_X23_X24_PAIR )
            printf("x23/24 ");
        if ( encoding & UNWIND_ARM64_FRAME_X25_X26_PAIR )
            printf("x25/26 ");
        if ( encoding & UNWIND_ARM64_FRAME_X27_X28_PAIR )
            printf("x27/28 ");
        if ( encoding & UNWIND_ARM64_FRAME_D8_D9_PAIR )
            printf("d8/9 ");
        if ( encoding & UNWIND_ARM64_FRAME_D10_D11_PAIR )
            printf("d10/11 ");
        if ( encoding & UNWIND_ARM64_FRAME_D12_D13_PAIR )
            printf("d12/13 ");
        if ( encoding & UNWIND_ARM64_FRAME_D14_D15_PAIR )
            printf("d14/15 ");
      }break;
      case UNWIND_ARM64_MODE_DWARF:
      {
        printf("dwarf offset 0x%08X, ", encoding & UNWIND_X86_64_DWARF_SECTION_OFFSET);
      }break;
      default:
      {
        if(encoding == 0 )
        {
            printf("no unwind info ");
        }
        else
        {
            printf("unknown arm64 compact encoding: %p ", encoding);
        }
      }break;
    }
    printf("\n");
}

internal struct unwind_info_regular_second_level_entry
compact_uwnd_lookup(void *unwind_info, U64 offset)
{
  // NOTE(yuraiz): The last page is "sentinel", it doesn't contain any values.

  // TODO(yuraiz): Use binary search
  struct unwind_info_regular_second_level_entry result = {};

  struct unwind_info_section_header *header =
    (struct unwind_info_section_header *)unwind_info;

  if(header->version != 1)
  {
    return result;
  }

  struct unwind_info_section_header_index_entry *entries =
    (struct unwind_info_section_header_index_entry *)(unwind_info + header->indexSectionOffset);

  // search entry
  U64 entry_index = 0;
  for EachIndex(i, header->indexCount)
  {
    if(entries[i].functionOffset > offset)
    {
      break;
    }
    entry_index = i;
  }

  // get the second level page
  struct unwind_info_regular_second_level_page_header *second_level_page =
    (struct unwind_info_regular_second_level_page_header *)(unwind_info + entries[entry_index].secondLevelPagesSectionOffset);

  switch(second_level_page->kind)
  {
    default:{}break;
    case UNWIND_SECOND_LEVEL_REGULAR:
    {
      struct unwind_info_regular_second_level_entry *second_level_entries =
        (struct unwind_info_regular_second_level_entry *)(IntFromPtr(second_level_page) + second_level_page->entryPageOffset);

      // search regular pages
      for EachIndex(j, second_level_page->entryCount)
      {
        struct unwind_info_regular_second_level_entry entry = second_level_entries[j];
        if(entry.functionOffset <= offset)
        {
          result = entry;
        }
        else
        {
          break;
        }
      }
    }break;
    case UNWIND_SECOND_LEVEL_COMPRESSED:
    {
      struct unwind_info_compressed_second_level_page_header *compressed_second_level_page =
        (struct unwind_info_compressed_second_level_page_header *)second_level_page;

      U32 *comp_entry = (U32 *)(IntFromPtr(second_level_page) + second_level_page->entryPageOffset);
      compact_unwind_encoding_t *encoding_entry = (U32 *)(IntFromPtr(second_level_page) + compressed_second_level_page->encodingsPageOffset);

      // search compressed pages
      for EachIndex(j, second_level_page->entryCount)
      {
        U64 relative_address = comp_entry[j] & 0xffffff;
        U64 function_offset = (U64)(entries[entry_index].functionOffset) + relative_address;

        if(function_offset <= offset)
        {
          result.functionOffset = function_offset;

          U32 opcode_index = comp_entry[j] >> 24;
          compact_unwind_encoding_t encoding = encoding_entry[opcode_index];
          result.encoding = encoding;
        }
        else
        {
          break;
        }
      }
    }break;
  }

  return result;
}

internal UWND_StepResult
compact_uwnd_step(Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *any_regs, U64 *cfa_out)
{
  UWND_StepResult result = {0};

  Temp scratch = scratch_begin(0, 0);

  ARM64_RegBlock *regs = any_regs;

  U64 pc = regs->pc - module_info->base_vaddr;

  // TODO(yuraiz): Check how "compact" the unwind info actually is, it may be expensive to read it all at once
  COMP_UWND_ModuleUnwindInfo *info = (COMP_UWND_ModuleUnwindInfo *)module_info->unwind_info;
  void *unwind_info = info->data.str;

  B32 is_good = 1;

  struct unwind_info_regular_second_level_entry entry = compact_uwnd_lookup(unwind_info, pc);
  compact_unwind_encoding_t encoding = entry.encoding;

  U32 encoding_mode = encoding & UNWIND_ARM64_MODE_MASK;

  switch(encoding_mode)
  {
    default:{
      // NOTE(yuraiz): For some reason sysctlbyname and some
      // other libc functions have that encoding.
      // The function offset we get is correct, so I'd suggest disassembling
      // instructions to figure out which registers to restore.
      if(encoding == 0x3)
      {
        regs->sp = regs->fp + 16;
        regs->pc = regs->lr;
      }
    }break;
    case UNWIND_ARM64_MODE_FRAMELESS:{
      U32 stack_size = ((encoding & UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK) >> 12) * 16;

      if(regs->pc != regs->lr)
      {      
        regs->sp += stack_size;
        regs->pc = regs->lr;
      }
    }break;
    case UNWIND_ARM64_MODE_DWARF:{
      U32 dwarf_offset = encoding & UNWIND_ARM64_DWARF_SECTION_OFFSET;
      fprintf(stderr, "oh, no, DWARF! %d", dwarf_offset);
      is_good = 0;
    }break;
    case UNWIND_ARM64_MODE_FRAME:{
      if(pc - entry.functionOffset < 4)
      {
        // TODO(yuraiz): Have a few fallbacks: for 1 instr, 2 instrs, 3, etc.
        // alternative: Disassemble and search if we hit stp instructon

        // NOTE(yuraiz): Fallback to the frameless unwinding for the first few instructions of the function
        regs->sp = regs->fp;
        regs->pc = regs->lr;
      }
      else
      {
        // frame-based unwinding
        U128 stack_frame = {0};
        U64 addr_size = sizeof(stack_frame);
        Rng1U64 stack_frame_vaddr_range = r1u64(regs->fp, regs->fp + addr_size);
        if(memory_map_read(memory_map, stack_frame_vaddr_range, &stack_frame) != addr_size)
        {
          is_good = 0;
          result.status = UWND_StepStatus_FailedMemoryRead;
          result.missed_read_vaddr_range = stack_frame_vaddr_range;
        }

        // NOTE(yuraiz): Return addresses on stack signed by arm64e ABI.
        // but I don't think that should be supported in the debugger.
        regs->sp = regs->fp + 16;
        regs->fp = stack_frame.u64[0];
        regs->pc = stack_frame.u64[1];

        // TODO(yuraiz): Unpack registers
        // if(encoding & UNWIND_ARM64_FRAME_X19_X20_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_X19_X20_PAIR\n");
        // }
        // if(encoding & UNWIND_ARM64_FRAME_X21_X22_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_X21_X22_PAIR\n");
        // }
        // if(encoding & UNWIND_ARM64_FRAME_X23_X24_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_X23_X24_PAIR\n");
        // }
        // if(encoding & UNWIND_ARM64_FRAME_X25_X26_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_X25_X26_PAIR\n");
        // }
        // if(encoding & UNWIND_ARM64_FRAME_X27_X28_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_X27_X28_PAIR\n");
        // }
        // if(encoding & UNWIND_ARM64_FRAME_D8_D9_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_D8_D9_PAIR\n");
        // }
        // if(encoding & UNWIND_ARM64_FRAME_D10_D11_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_D10_D11_PAIR\n");
        // }
        // if(encoding & UNWIND_ARM64_FRAME_D12_D13_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_D12_D13_PAIR\n");
        // }
        // if(encoding & UNWIND_ARM64_FRAME_D14_D15_PAIR)
        // {
        //   printf("    UNWIND_ARM64_FRAME_D14_D15_PAIR\n");
        // }
      }
    }break;
  }

  if(is_good)  {result.status |= UWND_StepStatus_Good;};
  scratch_end(scratch);
  return result;
}
