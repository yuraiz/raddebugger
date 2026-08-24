// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BINARY_NINJA_ARM64_IMPLEMENTATION
#include "third_party/binary-ninja-disassembler/arm64.h"

internal DASM_Inst
arm64_dasm_inst_from_code(Arena *arena, U64 vaddr, String8 code, DASM_Syntax syntax)
{
  DASM_Inst inst = {0};

  if(code.size >= 4)
  {
    char buf[2048] = {0};
    DASM_InstFlags flags = 0;
    U64 jump_dest_vaddr = 0;

    // TODO(yuraiz): Read additional information from the instruction.
    Instruction instr = {0};
    aarch64_decompose(*(U32*)code.str, &instr, vaddr);
    aarch64_disassemble(&instr, buf, sizeof(buf));

    // NOTE(yuraiz): Can be the jump destination, can be just an address
    U64 label_addr = 0;
    Register regs[4] = {0};
    for EachIndex(i, MAX_OPERANDS)
    {
      if(instr.operands[i].operandClass == LABEL)
      {
        label_addr = instr.operands[i].immediate;
      }
      if(instr.operands[i].operandClass == REG)
      {
        if(i < ArrayCount(regs))
        {
          regs[i] = instr.operands[i].reg[0];
        } 
      }
    }

    if(regs[0] == REG_SP)
    {
      flags |= DASM_InstFlag_ChangesStackPointer;
      // TODO(yuraiz): That's mostly lying, check for the immediate operand
      flags |= DASM_InstFlag_ChangesStackPointerVariably;
    }
    else
    {
      switch (instr.operation)
      {
        // call instructions
        case ARM64_BL:
        case ARM64_BLR:
        case ARM64_BLRAA:
        case ARM64_BLRAAZ:
        case ARM64_BLRAB:
        case ARM64_BLRABZ:
        {
          jump_dest_vaddr = label_addr;
          flags |= DASM_InstFlag_Call;
        }break;
        
        // jump instructions
        case ARM64_B:
        case ARM64_BR:
        case ARM64_BRAA:
        case ARM64_BRAAZ:
        case ARM64_BRAB:
        case ARM64_BRABZ:
        {
          jump_dest_vaddr = label_addr;
          flags |= DASM_InstFlag_UnconditionalJump;
        }break;

        // conditional branches
        case ARM64_B_AL:
        case ARM64_B_CC:
        case ARM64_B_CS:
        case ARM64_B_EQ:
        case ARM64_B_GE:
        case ARM64_B_GT:
        case ARM64_B_HI:
        case ARM64_B_LE:
        case ARM64_B_LS:
        case ARM64_B_LT:
        case ARM64_B_MI:
        case ARM64_B_NE:
        case ARM64_B_NV:
        case ARM64_B_PL:
        case ARM64_B_VC:
        case ARM64_B_VS:

        // compare and branch
        case ARM64_CBBEQ:
        case ARM64_CBBGE:
        case ARM64_CBBGT:
        case ARM64_CBBHI:
        case ARM64_CBBHS:
        case ARM64_CBBLE:
        case ARM64_CBBLO:
        case ARM64_CBBLS:
        case ARM64_CBBLT:
        case ARM64_CBBNE:
        case ARM64_CBEQ:
        case ARM64_CBGE:
        case ARM64_CBGT:
        case ARM64_CBHEQ:
        case ARM64_CBHGE:
        case ARM64_CBHGT:
        case ARM64_CBHHI:
        case ARM64_CBHHS:
        case ARM64_CBHI:
        case ARM64_CBHLE:
        case ARM64_CBHLO:
        case ARM64_CBHLS:
        case ARM64_CBHLT:
        case ARM64_CBHNE:
        case ARM64_CBHS:
        case ARM64_CBLE:
        case ARM64_CBLO:
        case ARM64_CBLS:
        case ARM64_CBLT:
        case ARM64_CBNE:
        case ARM64_CBNZ:
        case ARM64_CBZ:
        {
          jump_dest_vaddr = label_addr;
          flags |= DASM_InstFlag_Branch;
        }break;

        // return
        case ARM64_RET:
        case ARM64_RETAA:
        case ARM64_RETAASPPC:
        case ARM64_RETAASPPCR:
        case ARM64_RETAB:
        case ARM64_RETABSPPC:
        case ARM64_RETABSPPCR:
        {
          flags |= DASM_InstFlag_Return;
        }break;

        // store register pair and increment sp
        case ARM64_STP:
        {
          flags |= DASM_InstFlag_ChangesStackPointer;
          // stp fp, lr, [sp, #0x20]
          if(regs[0] == REG_X29 && regs[1] == REG_X30)
          {
            flags |= DASM_InstFlag_PushesArm64StackFrame;
          }
        }break;
        // load register pair and decrement sp
        case ARM64_LDP:
        {
          flags |= DASM_InstFlag_ChangesStackPointer;
        }break;

        default:
        {
          flags |= DASM_InstFlag_NonFlow;
        }break;
      }
    }

    String8List flag_list = {0};
    if(flags & DASM_InstFlag_Call) {
      str8_list_push(arena, &flag_list, str8_lit("call"));
    }
    if(flags & DASM_InstFlag_Branch) {
      str8_list_push(arena, &flag_list, str8_lit("branch"));
    }
    if(flags & DASM_InstFlag_UnconditionalJump) {
      str8_list_push(arena, &flag_list, str8_lit("jump"));
    }
    if(flags & DASM_InstFlag_Return) {
      str8_list_push(arena, &flag_list, str8_lit("ret"));
    }
    if(flags & DASM_InstFlag_NonFlow) {
      str8_list_push(arena, &flag_list, str8_lit("n/f"));
    }
    if(flags & DASM_InstFlag_Repeats) {
      str8_list_push(arena, &flag_list, str8_lit("repeats"));
    }
    if(flags & DASM_InstFlag_ChangesStackPointer) {
      str8_list_push(arena, &flag_list, str8_lit("sp"));
    }
    if(flags & DASM_InstFlag_ChangesStackPointerVariably) {
      str8_list_push(arena, &flag_list, str8_lit("sp/var"));
    }
    if(flags & DASM_InstFlag_PushesArm64StackFrame)
    {
      str8_list_push(arena, &flag_list, str8_lit("s/frame"));
    }

    StringJoin flags_join = {0};
    flags_join.sep = str8_lit(",");
    String8 flag_string = str8_list_join(arena, &flag_list, &flags_join);

    String8 result = str8_cstring(buf);
    if (flags != DASM_InstFlag_NonFlow)
    {
      result = push_str8f(arena, "%S (flags=%S)", result, flag_string);
    }

    //////////////////////////////
    //- rjf: bundle
    //
    {
      inst.flags           = flags;
      inst.size            = sizeof(U32);
      inst.string          = str8_copy(arena, result);
      inst.dst_vaddr       = jump_dest_vaddr;
      // inst.dst_reg_code    = dst_reg_code;
      // inst.dst_reg_off     = dst_reg_off;
      // inst.src_reg_code    = src_reg_code;
      // inst.src_reg_off     = src_reg_off;
    }
  }
  
  return inst;
}
