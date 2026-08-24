// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal UWND_StepResult
compact_uwnd_step(Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *any_regs, U64 *cfa_out)
    // D_Entity *module, D_Handle process_handle, REGS_RegBlockARM64 *regs, U64 endt_us)
{
  ARM64_RegBlock *regs = any_regs;

  UWND_StepResult result = {0};

  //////////////////////////////
  // NOTE(yuraiz): From https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
  // > The frame pointer register (x29) must always address a valid frame record.
  // > Some functions — such as leaf functions or tail calls — may opt not to create an entry in this list.
  // > As a result, stack traces are always meaningful, even without debug information.
  // 
  // That simplifies unwinding.
  // 
  // Usually the standard prologue looks something like that:
  // sub sp, sp, #0x30
  // stp fp, lr, [sp, #0x20]
  // add fp, sp, #0x20
  //
  // Currently a function is assumed to be a leaf function if it doesn't contain
  // `stp` as one of the first instructions. or if it hasn't been executed.

  Temp scratch = scratch_begin(0, 0);

  // check if we're in a leaf function
  B32 is_leaf = 0;
  // {
  //   // lookup the current procedure in the debug info
  //   U64 rip_vaddr = regs->pc;
  //   Access *access = access_open();
  //   U64 rip_voff = d_voff_from_vaddr(module, rip_vaddr);
  //   DI_Key dbgi_key = d_dbgi_key_from_module(module);
  //   RDI_Parsed *rdi = di_rdi_from_key(access, dbgi_key, 0, 0);
  //   RDI_Symbol *procedure = rdi_procedure_from_voff(rdi, rip_voff);
  //   U64 addr = rdi_first_voff_from_procedure(rdi, procedure);
    
  //   // disassemble a few instructions
  //   U64 instr_count = 4;
  //   U64 instr_addr = module->vaddr_range.min + addr;
  //   Rng1U64 vrange = {instr_addr, instr_addr + sizeof(U32) * instr_count};
  //   String8 data = dmn_process_read_block(scratch.arena, process_handle.dmn_handle, vrange);

  //   // search for the first stp instruction
  //   U64 stp_vaddr = 0;
  //   for(U64 off = 0; off < data.size;)
  //   {
  //     DASM_Inst inst = dasm_inst_from_code(scratch.arena, Arch_arm64, 0, str8_skip(data, off), DASM_Syntax_Intel);

  //     if(str8_match_lit("stp", str8_prefix(inst.string, 3), 0))
  //     {
  //       stp_vaddr = instr_addr + off;
  //     }

  //     if(inst.size == 0 || stp_vaddr != 0 || inst.flags & DASM_InstFlag_Return)
  //     {
  //       break;
  //     }

  //     off += inst.size;
  //   }

  //   // "leaf" if we correctly got the debug info and stp wasn't found / not yet executed
  //   is_leaf = procedure != 0 && (stp_vaddr == 0 || stp_vaddr > rip_vaddr);
  // }

  B32 is_good = 1;
  B32 is_stale = 0;

  U64 fp = regs->fp;

  // validate fp
  if ((fp == 0) || (!(((fp) & 0xf) == 0)))
  {
    is_good = 0;
  }
  
  if(is_leaf)
  {
    // frameless unwinding
    regs->pc = regs->lr;
    regs->sp = fp;
  }
  else
  {
    // frame-based unwinding
    U128 stack_frame = {0};
    U64 addr_size = sizeof(stack_frame);
    Rng1U64 stack_frame_vaddr_range = r1u64(fp, fp + addr_size);
    if(memory_map_read(memory_map, stack_frame_vaddr_range, &stack_frame) != addr_size)
    {
      is_good = 0;
      result.status = UWND_StepStatus_FailedMemoryRead;
      result.missed_read_vaddr_range = stack_frame_vaddr_range;
    }
    
    // NOTE(yuraiz): Return addresses on stack signed by arm64e ABI.
    // but I don't think that should be supported in the debugger.
    regs->fp = stack_frame.u64[0];
    regs->pc = stack_frame.u64[1];
    regs->sp = fp;
  }

  if(is_good)  {result.status |= UWND_StepStatus_Good;};
  scratch_end(scratch);
  return result;
}
