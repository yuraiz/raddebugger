// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BINARY_NINJA_ARM64_IMPLEMENTATION
#include "third_party/binary-ninja-disassembler/arm64.h"

internal ARM64_RegCode
arm64_reg_code_from_bnd(Register reg)
{
  ARM64_RegCode out_code = ARM64_RegCode_nil;

  // regular registers
  if(reg >= REG_W0 && reg <= REG_WSP)
  {
    out_code = reg - REG_W0 + ARM64_RegCode_x0;
  }
  else if(reg >= REG_X0 && reg <= REG_X30)
  {
    out_code = reg - REG_X0 + ARM64_RegCode_x0;
  }
  else if(reg == REG_SP)
  {
    out_code = ARM64_RegCode_sp;
  }

  // vector registers
  else if(reg >= REG_V0 && reg <= REG_Q31)
  {
    U32 reg_index = (reg - REG_V0) % 32;
    // V, B, H, S, D, 
    U32 reg_size_index = (reg - REG_V0) / 32;
    out_code = reg - REG_V0 + ARM64_RegCode_v0;
  }  

  // B vector
  else if(reg >= REG_V0_B0 && reg <= REG_V31_B15)
  {
    U32 reg_index = (reg - REG_V0_B0) / 16;
    U32 b_index = (reg - REG_V0_B0) % 16;
    out_code = reg_index + ARM64_RegCode_v0;
  }

  // H vector
  else if(reg >= REG_V0_H0 && reg <= REG_V31_H7)
  {
    U32 reg_index = (reg - REG_V0_H0) / 8;
    U32 h_index = (reg - REG_V0_H0) % 8;
    out_code = reg_index + ARM64_RegCode_v0;
  }

  // S vector
  else if(reg >= REG_V0_S0 && reg <= REG_V31_S3)
  {
    U32 reg_index = (reg - REG_V0_S0) / 4;
    U32 s_index = (reg - REG_V0_S0) % 4;
    out_code = reg_index + ARM64_RegCode_v0;
  }

  // D vector
  else if(reg >= REG_V0_D0 && reg <= REG_V31_D1)
  {
    U32 reg_index = (reg - REG_V0_D0) / 2;
    U32 s_index = (reg - REG_V0_D0) % 2;
    out_code = reg_index + ARM64_RegCode_v0;
  }

  // SVE regs aren't supported yet

  return out_code;
}

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
    String8 result = str8_cstring(buf);

    // NOTE(yuraiz): Can be the jump destination, can be just an address
    U64 label_addr = 0;
    Register regs[MAX_OPERANDS * MAX_REGISTERS] = {};
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

    ARM64_RegCode dst_reg_code = ARM64_RegCode_nil;
    ARM64_RegCode src_reg_code = ARM64_RegCode_nil;
    S64 dst_reg_off = 0;
    S64 src_reg_off = 0;

    //- yuraiz: decode register parameters
    {
      ARM64_RegCode reg1 = arm64_reg_code_from_bnd(instr.operands[1].reg[0]);
      S64 imm1 = instr.operands[1].immediate;
      ARM64_RegCode reg2 = arm64_reg_code_from_bnd(instr.operands[2].reg[0]);
      S64 imm2 = instr.operands[2].immediate;

      switch (instr.operation)
      {
        default:{}break;
        case ARM64_LDR:   case ARM64_LDUR:
        case ARM64_LDRB:  case ARM64_LDRSB:
        case ARM64_LDRH:  case ARM64_LDRSH:
        case ARM64_LDRSW:
        {
          src_reg_code = reg1;
          src_reg_off = imm1;
        }break;
        case ARM64_STR:   case ARM64_STUR:
        case ARM64_STRB:  case ARM64_STRH:
        {
          dst_reg_code = reg1;
          dst_reg_off = imm1;
        }break;
        case ARM64_LDP:   case ARM64_LDNP:
        {
          src_reg_code = reg2;
          src_reg_off = imm2;
        }break;
        case ARM64_STP:   case ARM64_STNP:
        {
          dst_reg_code = reg2;
          dst_reg_off = imm2;
        }break;
      }
    }

    if(regs[0] == REG_SP || regs[1] == REG_SP || regs[2] == REG_SP)
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

    //////////////////////////////
    //- rjf: bundle
    //
    {
      inst.flags           = flags;
      inst.size            = sizeof(U32);
      inst.string          = str8_copy(arena, result);
      inst.dst_vaddr       = jump_dest_vaddr;
      inst.dst_reg_code    = dst_reg_code;
      inst.dst_reg_off     = dst_reg_off;
      inst.src_reg_code    = src_reg_code;
      inst.src_reg_off     = src_reg_off;
    }
  }
  
  return inst;
}
