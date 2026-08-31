// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// NOTE(yuraiz): unwind info compresses entries for groups of functions.
// So to figure out how to unwind precisely the current place in the function
// needs to be analyzed.

// TODO(yuraiz): handle case like that, generally 
// it doesn't look that hard to emulate "ldp" and "add"
// call sysconf(_SC_PAGESIZE); for reference
//
//   ldp fp, lr, [sp, #0x70] (flags=sp)
//   ldp x20, x19, [sp, #0x60] (flags=sp)
//   ldp x22, x21, [sp, #0x50] (flags=sp)
//   add sp, sp, #0x80 (flags=sp,sp/var)
// 
// I think it can be handled similar to stp -> read back/front and
// decode the immediate, restore the registers and increase sp.

typedef enum COMP_UWND_ARM64InstCategory COMP_UWND_ARM64InstCategory;
enum COMP_UWND_ARM64InstCategory {
  COMP_UWND_ARM64InstCategory_none,

  // Function prologue

  // pacibsp
  COMP_UWND_ARM64InstCategory_pacibsp,
  // sub sp, sp, #imm
  COMP_UWND_ARM64InstCategory_sub_sp_sp,
  // stp d15, d14, [sp, #imm]! - modifies the stack pointer
  COMP_UWND_ARM64InstCategory_stp_regs_wback,
  // stp d15, d14, [sp, #imm]
  COMP_UWND_ARM64InstCategory_stp_regs,
  // stp fp, lr, [sp, #imm]
  // stp fp, lr, [sp, #imm]!
  COMP_UWND_ARM64InstCategory_frame_push,
  // add fp, sp, #imm
  COMP_UWND_ARM64InstCategory_add_fp_sp,
  
  // function epilogue

  // ldp fp, lr, [sp, #imm]
  // ldp fp, lr, [sp], #imm
  COMP_UWND_ARM64InstCategory_frame_pop,
  // TODO(yuraiz): ldp regs, similar to stp
  // add sp, sp, #imm
  COMP_UWND_ARM64InstCategory_add_sp_sp,
  // ret, retab
  COMP_UWND_ARM64InstCategory_ret,
};

internal COMP_UWND_ARM64InstCategory
compact_uwnd_analyze_instr(U32 instr)
{
  COMP_UWND_ARM64InstCategory result = COMP_UWND_ARM64InstCategory_none;

  //- yuraiz: check prologue instructions

  if(instr == 0xd503237f)
  {
    // pacibsp
    result = COMP_UWND_ARM64InstCategory_pacibsp;
  }
  
  if((instr & 0xffc003ff) == 0xd10003ff)
  {
    // sub	sp, sp, #imm
    result = COMP_UWND_ARM64InstCategory_sub_sp_sp;
  }

  U32 with_stp_mask = instr & 0xffc07fff;

  switch(with_stp_mask)
  {
    default:{}break;
    // stp d15, d14, [sp, #0]
    case 0x6d003bef:
    // stp d13, d12, [sp, #0]
    case 0x6d0033ed:
    // stp d11, d10, [sp, #0]
    case 0x6d002beb:
    // stp d9, d8, [sp, #0]
    case 0x6d0023e9:
    // stp x28, x27 [sp, #0]
    case 0xa9006ffc:
    // stp x26, x25, [sp, #0]
    case 0xa90067fa:
    // stp x24, x23, [sp, #0]
    case 0xa9005ff8:
    // stp x22, x21, [sp, #0]
    case 0xa90057f6:
    // stp x20, x19, [sp, #0]
    case 0xa9004ff4:
    {
      result = COMP_UWND_ARM64InstCategory_stp_regs;
    }break;

    // stp R1, R2, [sp, #0]!
    case 0x6d803bef:
    case 0x6d8033ed:
    case 0x6d802beb:
    case 0x6d8023e9:
    case 0xa9806ffc:
    case 0xa98067fa:
    case 0xa9805ff8:
    case 0xa98057f6:
    case 0xa9804ff4:
    {
      result = COMP_UWND_ARM64InstCategory_stp_regs_wback;
    }break;
  }

  // stp fp, lr, [sp, #imm]
  if((instr & 0xffc07fff) == 0xa9007bfd)
  {
    result = COMP_UWND_ARM64InstCategory_frame_push;
  }

  // stp fp, lr, [sp, #imm]!
  if((instr & 0xffc07fff) == 0xa9807bfd)
  {
    result = COMP_UWND_ARM64InstCategory_frame_push;
  }

  // add fp, sp, #imm
  if((instr & 0xffc003ff) == 0x910003fd)
  {
    result = COMP_UWND_ARM64InstCategory_add_fp_sp;
  }
  
  //- yuraiz: check epilogue instructions

  // ldp fp, lr, [sp, #val]
  // ldp fp, lr, [sp], #val
  if(((instr) & 0xffc07fff) == 0xa9407bfd || ((instr) & 0xffc07fff) == 0xa8c07bfd)
  {
    result = COMP_UWND_ARM64InstCategory_frame_pop;
  }

  // add sp, sp, #imm
  if((instr & 0xffc003ff) == 0x910003ff)
  {
    result = COMP_UWND_ARM64InstCategory_add_sp_sp;
  }

  // ret, retab
  if(((instr) == 0xd65f03c0) || ((instr) == 0xd65f0fff))
  {
    result = COMP_UWND_ARM64InstCategory_ret;
  }

  return result;
}

internal S64
compact_uwnd_get_immediate(U32 instr, COMP_UWND_ARM64InstCategory category)
{
  S64 result = 0;
  switch(category)
  {
    default:{}break;
    case COMP_UWND_ARM64InstCategory_add_fp_sp:
    case COMP_UWND_ARM64InstCategory_add_sp_sp:
    case COMP_UWND_ARM64InstCategory_sub_sp_sp:
    {
      // add/sub immediate
      result = (((S64)instr & 0x3ffc00) >> 10) & 0xfff;
    }break;
    case COMP_UWND_ARM64InstCategory_stp_regs_wback:
    case COMP_UWND_ARM64InstCategory_stp_regs:
    case COMP_UWND_ARM64InstCategory_frame_push:
    {
      // stp immediate
      result = ((((S64)instr & 0x3f8000) >> 15) & 0x7f);
      // two's complement
      if (result & 0x40) {
        result -= 0x80;
      }
      // multiple of 8
      result *= 8;
    }break;
  }
  return result;
}

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

internal compact_unwind_encoding_t
compact_uwnd_lookup(void *unwind_info, U64 offset)
{
  // NOTE(yuraiz): The last page is "sentinel", it doesn't contain any values.

  // TODO(yuraiz): Use binary search

  compact_unwind_encoding_t result = 0;

  struct unwind_info_section_header *header =
    (struct unwind_info_section_header *)unwind_info;

  if(header->version != 1 || header->indexCount == 0)
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

  const compact_unwind_encoding_t* common_encodings = (compact_unwind_encoding_t*)(unwind_info + header->commonEncodingsArraySectionOffset);

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
          result = entry.encoding;
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
      compact_unwind_encoding_t *page_encodings = (U32 *)(IntFromPtr(second_level_page) + compressed_second_level_page->encodingsPageOffset);

      // search compressed pages
      for EachIndex(j, second_level_page->entryCount)
      {
        U64 relative_address = comp_entry[j] & 0xffffff;
        U64 function_offset = (U64)(entries[entry_index].functionOffset) + relative_address;

        if(function_offset <= offset)
        {
          U32 encoding_index = comp_entry[j] >> 24;
          compact_unwind_encoding_t encoding = page_encodings[encoding_index];

          if (encoding_index < header->commonEncodingsArrayCount)
          {
            result = common_encodings[encoding_index];
          }
          else
          {
            result = page_encodings[encoding_index - header->commonEncodingsArrayCount];
          }
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

  //- yuraiz: fp == 0 at the lowest frame of a secondary stack, do not try to read anything
  if(regs->fp == 0)
  {
    result.status = UWND_StepStatus_Good;
    return result;
  }

  // TODO(yuraiz): Check how "compact" the unwind info actually is, it may be expensive to read it all at once
  COMP_UWND_ModuleUnwindInfo *info = (COMP_UWND_ModuleUnwindInfo *)module_info->unwind_info;
  void *unwind_info = info->data.str;

  B32 is_good = 1;

  compact_unwind_encoding_t encoding = compact_uwnd_lookup(unwind_info, pc);


  if(encoding == 0x0)
  {
    // NOTE(yuraiz): External calls don't have unwind info entries.
    // So check if we're in an externall call.
    //
    // May also check all three of the instructions in order, but I'm not sure it's needed.
    //
    // external call looks like this:
    // adrp	x16, __external_func_table
    // ldr	x16, [x16, #offset]
    // br	x16
    //
    // there's also that external call pattern, with pointer authentification
    // adrp	x17, #__external_func_table
    // add	x17, x17, #offset
    // ldr	x16, [x17]
    // braa	x16, x17

    U32 instr = 0;
    Rng1U64 code_vaddr_range = r1u64(regs->pc, regs->pc + sizeof(instr));
    if(memory_map_read(memory_map, code_vaddr_range, &instr) != sizeof(instr))
    {
      is_good = 0;
      result.status = UWND_StepStatus_FailedMemoryRead;
      result.missed_read_vaddr_range = code_vaddr_range;
    }

    // check for regular external call
    B32 is_adrp_x16 = (instr & 0x9f00001f) == 0x90000010;
    B32 is_ldr_x16_by_x16 = (instr & 0xfec007ff) == 0xf8400210;
    B32 is_br_x16 = instr == 0xd61f0200;

    B32 is_external_call = is_adrp_x16 || is_ldr_x16_by_x16 || is_br_x16;

    // check for pac external call
    B32 is_adrp_x17 = (instr & 0x9f00001f) == 0x90000011;
    B32 is_add_x17_x17 = (instr & 0xffc003ff) == 0x91000231;
    B32 is_ldr_x16_by_x17 = instr == 0xf9400230;
    B32 is_braa_x16_x17 = instr == 0xd71f0a11;

    is_external_call |= is_adrp_x17 | is_add_x17_x17 | is_ldr_x16_by_x17 | is_braa_x16_x17;

    if(is_external_call)
    {
      //- yuraiz: just restore pc
      regs->pc = regs->lr;
    }
    else
    {
      // search forward for return to check what is restored

      B32 pop_frame = 0;
      B32 found_ret = 0;
      S64 sp_offset = 0;
      B32 is_syscall = 0;

      U64 start = regs->pc - sizeof(instr);

      for EachIndex(idx, 32)
      {
        U32 instr = 0;
        Rng1U64 code_vaddr_range = r1u64(start + idx * sizeof(instr), start + idx * sizeof(instr) + sizeof(instr));
        if(memory_map_read(memory_map, code_vaddr_range, &instr) != sizeof(instr))
        {
          is_good = 0;
          result.status = UWND_StepStatus_FailedMemoryRead;
          result.missed_read_vaddr_range = code_vaddr_range;
          break;
        }

        if(instr == 0xd4001001)
        {
          is_syscall = 1;
          break;
        }

        COMP_UWND_ARM64InstCategory category = compact_uwnd_analyze_instr(instr);

        if(category == COMP_UWND_ARM64InstCategory_frame_pop)
        {
          pop_frame = 1;
        }
        if(category == COMP_UWND_ARM64InstCategory_add_sp_sp)
        {
          sp_offset += compact_uwnd_get_immediate(instr, category);
        }
        if(category == COMP_UWND_ARM64InstCategory_ret)
        {
          found_ret = 1;
          break;
        }
      }

      if(is_syscall)
      {
        encoding = UNWIND_ARM64_MODE_FRAMELESS;
      }
      else if(found_ret && !pop_frame)
      {
        encoding = UNWIND_ARM64_MODE_FRAMELESS;
      }
      else
      {
        encoding = UNWIND_ARM64_MODE_FRAME;
      }
    }
  }

  switch(encoding & UNWIND_ARM64_MODE_MASK)
  {
    default:{
      
    }break;
    case UNWIND_ARM64_MODE_FRAMELESS:{
      U32 stack_size = ((encoding & UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK) >> 12) * 16;
      regs->sp += stack_size;
      regs->pc = regs->lr;
    }break;
    case UNWIND_ARM64_MODE_DWARF:{
      U32 dwarf_offset = encoding & UNWIND_ARM64_DWARF_SECTION_OFFSET;
      // TODO(yuraiz): Unwind using dwarf
      is_good = 0;
    }break;
    case UNWIND_ARM64_MODE_FRAME:{
      // NOTE(yuraiz): unwind info compresses entries for groups of functions.
      // So analyze a few instructions to check if we're on the prologue.

      U32 instr = 0;
      Rng1U64 code_vaddr_range = r1u64(regs->pc, regs->pc + sizeof(instr));
      if(memory_map_read(memory_map, code_vaddr_range, &instr) != sizeof(instr))
      {
        is_good = 0;
        result.status = UWND_StepStatus_FailedMemoryRead;
        result.missed_read_vaddr_range = code_vaddr_range;
      }

      COMP_UWND_ARM64InstCategory category = compact_uwnd_analyze_instr(instr);

      //- yuraiz: the frame was just pushed
      //
      // sometimes happens in hand-written asm:
      // stp fp, lr, [sp, #-16]!
      // mov fp, sp
      // sub sp, sp, #288
      if(category == COMP_UWND_ARM64InstCategory_sub_sp_sp)
      {
        if(regs->fp == regs->sp)
        {
          category = COMP_UWND_ARM64InstCategory_none;
        }
      }

      switch(category)
      {
        default:{}break;
        case COMP_UWND_ARM64InstCategory_none:
        {
          //- yuraiz: in the function body – unwind normally
        
          //- yuraiz: unpack registers
          
          //- yuraiz: count the register pairs * 2
          U64 stored_registers = count_bits_set32(encoding & 0xf1f) * 2;

          //- yuraiz: max 9 pairs
          U64 register_pairs[18] = {};

          //- yuraiz: read the stored registers
          U64 range_end = regs->fp - 8;
          U64 range_start = range_end - stored_registers * 8;
          Rng1U64 vaddr_range = r1u64(range_start, range_end);
          if(memory_map_read(memory_map, vaddr_range, &register_pairs) != dim_1u64(vaddr_range))
          {
            is_good = 0;
            result.status = UWND_StepStatus_FailedMemoryRead;
            result.missed_read_vaddr_range = vaddr_range;
          }

          U64 idx = stored_registers - 1;

          //- yuraiz: restore general-purpose registers
          if (encoding & UNWIND_ARM64_FRAME_X19_X20_PAIR) {
            regs->x19 = register_pairs[idx--];
            regs->x20 = register_pairs[idx--];
          }
          if (encoding & UNWIND_ARM64_FRAME_X21_X22_PAIR) {
            regs->x21 = register_pairs[idx--];
            regs->x22 = register_pairs[idx--];
          }
          if (encoding & UNWIND_ARM64_FRAME_X23_X24_PAIR) {
            regs->x23 = register_pairs[idx--];
            regs->x24 = register_pairs[idx--];
          }
          if (encoding & UNWIND_ARM64_FRAME_X25_X26_PAIR) {
            regs->x25 = register_pairs[idx--];
            regs->x26 = register_pairs[idx--];
          }
          if (encoding & UNWIND_ARM64_FRAME_X27_X28_PAIR) {
            regs->x27 = register_pairs[idx--];
            regs->x28 = register_pairs[idx--];
          }
          
          //- yuraiz: restore floating-point registers
          if (encoding & UNWIND_ARM64_FRAME_D8_D9_PAIR) {
            regs->v8.u64[0] = register_pairs[idx--];
            regs->v9.u64[0] = register_pairs[idx--];
          }
          if (encoding & UNWIND_ARM64_FRAME_D10_D11_PAIR) {
            regs->v10.u64[0] = register_pairs[idx--];
            regs->v11.u64[0] = register_pairs[idx--];
          }
          if (encoding & UNWIND_ARM64_FRAME_D12_D13_PAIR) {
            regs->v12.u64[0] = register_pairs[idx--];
            regs->v13.u64[0] = register_pairs[idx--];
          }
          if (encoding & UNWIND_ARM64_FRAME_D14_D15_PAIR) {
            regs->v14.u64[0] = register_pairs[idx--];
            regs->v15.u64[0] = register_pairs[idx--];
          }

          U128 stack_frame = {0};
          U64 addr_size = sizeof(stack_frame);
          Rng1U64 stack_frame_vaddr_range = r1u64(regs->fp, regs->fp + addr_size);
          if(memory_map_read(memory_map, stack_frame_vaddr_range, &stack_frame) != addr_size)
          {
            is_good = 0;
            result.status = UWND_StepStatus_FailedMemoryRead;
            result.missed_read_vaddr_range = stack_frame_vaddr_range;
          }
          
          // old sp is fp + stack frame size
          regs->sp = regs->fp + 16;
          // pop previous fp
          regs->fp = stack_frame.u64[0];
          // pop return address into pc
          regs->pc = stack_frame.u64[1];
        }break;

        case COMP_UWND_ARM64InstCategory_pacibsp:
        {
          regs->pc = regs->lr;
        }break;
        case COMP_UWND_ARM64InstCategory_sub_sp_sp:
        {
          regs->pc = regs->lr;
        }break;
        case COMP_UWND_ARM64InstCategory_stp_regs:
        {
          // search backwards for sub_sp_sp or stp_regs_wback
          // there's max 10 instructions between the current position and the target instruction
          S64 sp_offset = 0;
          for EachIndex(idx, 10)
          {
            U32 instr = 0;
            Rng1U64 code_vaddr_range = r1u64(regs->pc - idx, regs->pc - idx + sizeof(instr));
            if(memory_map_read(memory_map, code_vaddr_range, &instr) != sizeof(instr))
            {
              is_good = 0;
              result.status = UWND_StepStatus_FailedMemoryRead;
              result.missed_read_vaddr_range = code_vaddr_range;
              break;
            }

            COMP_UWND_ARM64InstCategory category = compact_uwnd_analyze_instr(instr);
            if(category == COMP_UWND_ARM64InstCategory_sub_sp_sp)
            {
              sp_offset = compact_uwnd_get_immediate(instr, category);
            }
            else if(category == COMP_UWND_ARM64InstCategory_stp_regs_wback)
            {
              // the immediate is signed and negative, so revert it
              sp_offset = -compact_uwnd_get_immediate(instr, category);
            }
          }

          // restore sp based on the value
          regs->sp += sp_offset;
          regs->pc = regs->lr;
        }break;
        case COMP_UWND_ARM64InstCategory_stp_regs_wback:
        {
          // just after the call, not changed sp yet
          regs->pc = regs->lr;
        }break;
        case COMP_UWND_ARM64InstCategory_frame_push:
        {
          S64 fp_offset = compact_uwnd_get_immediate(instr, category);
          regs->sp += fp_offset + 16;
          regs->pc = regs->lr;
        }break;
        case COMP_UWND_ARM64InstCategory_add_fp_sp:
        {
          S64 fp_offset = compact_uwnd_get_immediate(instr, category);
          regs->sp += fp_offset + 16;
          regs->pc = regs->lr;
        }break;
        case COMP_UWND_ARM64InstCategory_frame_pop:
        {
          // the stack frame is still valid
          U128 stack_frame = {0};
          U64 addr_size = sizeof(stack_frame);
          Rng1U64 stack_frame_vaddr_range = r1u64(regs->fp, regs->fp + addr_size);
          if(memory_map_read(memory_map, stack_frame_vaddr_range, &stack_frame) != addr_size)
          {
            is_good = 0;
            result.status = UWND_StepStatus_FailedMemoryRead;
            result.missed_read_vaddr_range = stack_frame_vaddr_range;
          }
          
          // old sp is fp - stack frame size
          regs->sp = regs->fp + 16;
          // pop previous fp
          regs->fp = stack_frame.u64[0];
          // pop return address into pc
          regs->pc = stack_frame.u64[1];
        }break;
        case COMP_UWND_ARM64InstCategory_add_sp_sp:
        {
          // just popped fp and lr, only need to restore sp and pc
          S64 imm = compact_uwnd_get_immediate(instr, category);
          // "execute" the current instruction
          regs->sp += imm;
          // pop return address into pc
          regs->pc = regs->lr;
        }break;
        case COMP_UWND_ARM64InstCategory_ret:
        {
          regs->pc = regs->lr;
        }break;
      }
    }break;
  }

  // NOTE(yuraiz): Return addresses on stack signed by arm64e ABI.
  // but I don't think that should be supported in the debugger.
  //
  // regs->pc = remove_pac_somehow(regs->pc) ???

  if(is_good)  {result.status |= UWND_StepStatus_Good;};
  scratch_end(scratch);
  return result;
}
