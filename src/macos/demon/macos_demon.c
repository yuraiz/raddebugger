// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Includes

#include <mach/mach_vm.h>

#include "macos/demon/macos_demon_exc.c"

// NOTE(yuraiz): c++ symbol demangling function.
//
// The problem that it uses a function from libc++abi, ideally I wouldn't want to link to it,
// but it isn't hard to revert that change – it's only 30 loc.
extern char *__cxa_demangle(const char *mangled_name,
                            char *output_buffer,
                            size_t *length,
                            int *status);

// private flag to disable aslr
#ifndef _POSIX_SPAWN_DISABLE_ASLR
# define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

internal MAC_DMN_ActiveTrap *
mac_dmn_try_set_trap(Arena *arena, DMN_Trap *trap)
{
  // NOTE(yuraiz): On macOS reading or writing to the memory fails fairly often.
  MAC_DMN_ActiveTrap *result = 0;
  ARCH_Info *arch = arch_info_from_arch(Arch_CURRENT);
  String8 trap_inst = arch->trap_instruction;
  U8 *swap_bytes = push_array(arena, U8, trap_inst.size);
  if(dmn_process_read(trap->process, r1u64(trap->vaddr, trap->vaddr + trap_inst.size), swap_bytes) == trap_inst.size)
  {
    // NOTE(yuraiz): Initially the check was `== trap_inst.size`, but since the type is B32 I assume that's a mistake.
    if(dmn_process_write(trap->process, r1u64(trap->vaddr, trap->vaddr + trap_inst.size), trap_inst.str))
    {
      result = push_array(arena, MAC_DMN_ActiveTrap, 1);
      result->trap       = trap;
      result->swap_bytes = str8(swap_bytes, trap_inst.size);
    }
    else
    {
      fprintf(stderr, "failed to write trap instruction\n");
    }
  }
  else
  {
    fprintf(stderr, "failed to read original bytes\n");
  }
  return result;
}

internal B32
mac_dmn_write_to_protected(
    const task_t task,
    const mach_vm_address_t address,
    const void* data,
    const size_t size,
    const bool revert_back
) {
  // NOTE(yuraiz): on macOS memory writable XOR executable.
  // so we need to change the protection before writing to it and revert back after.

  mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
  vm_region_submap_short_info_data_64_t region_info = {0};

  mach_vm_address_t region_address = address;
  mach_vm_size_t region_size = (mach_vm_size_t)size;
  U32 nesting_depth = 999999;

  kern_return_t kr = 0;

	kr = mach_vm_region_recurse(
    task,
    &region_address,
    &region_size,
    &nesting_depth,
    (vm_region_recurse_info_t)&region_info,
    &count
  );

  if(kr != 0)
  {
    printf("failed to query memory region %p..%p: %s\n", address, address + size, mach_error_string(kr));
  }

  vm_prot_t old_protection = region_info.protection;

  bool needs_to_change_protection =
      ((old_protection & VM_PROT_WRITE) == 0 ||
        (old_protection & VM_PROT_EXECUTE) != 0);

  bool executable_protection_modified = false;
  if (needs_to_change_protection) {
    vm_prot_t new_protection = 0;

    if ((old_protection & VM_PROT_EXECUTE) != 0) {
      new_protection =
          (old_protection & ~VM_PROT_EXECUTE) | VM_PROT_WRITE;
      executable_protection_modified = true;

      task_suspend(task);
    } else {
      new_protection = (old_protection | VM_PROT_WRITE);
    }

    if(region_info.max_protection & VM_PROT_WRITE)
    {
      // the memory can be made writeable
      kr = mach_vm_protect(
        task,
        region_address,
        region_size,
        0,
        new_protection
      );
    }
    else
    {
      // use copy-on-write to elevate the max protection and make the region writeable
      kr = mach_vm_protect(
        task,
        region_address,
        region_size,
        0,
        VM_PROT_COPY | VM_PROT_READ | VM_PROT_WRITE
      );
    }

    if(kr != 0)
    {
      printf("%x -> %x (max: %x)\n", old_protection, new_protection, region_info.max_protection);
      printf("failed change protection %p..%p: %s\n", region_address, region_address + region_size, mach_error_string(kr));
    }
  }

  kr = mach_vm_write(
    task,
    address,
    (vm_offset_t)data,
    (mach_msg_type_number_t)size
  );

  // flush from the caches
  vm_machine_attribute_val_t value = MATTR_VAL_CACHE_FLUSH;
  mach_vm_machine_attribute(
      task,
      region_address,
      region_size,
      MATTR_CACHE,
      &value
  );

  // re-protect the region back to the way it was
  if ((revert_back || executable_protection_modified) &&
      needs_to_change_protection) {
    mach_vm_protect(
      task,
      region_address,
      region_size,
      0,
      old_protection
    );

    if (executable_protection_modified) {
        task_resume(task);
    }
  }

  return kr == 0;
}

internal kern_return_t
mac_dmn_vm_region_recurse_deepest(
  mach_port_t task,
  mach_vm_address_t* address,
  mach_vm_size_t* size,
  natural_t* depth,
  vm_prot_t* protection,
  unsigned int* user_tag)
{
  struct vm_region_submap_short_info_64 submap_info;
  mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
  while (true)
  {
    kern_return_t status_code =
      mach_vm_region_recurse(task, address, size, depth, (vm_region_recurse_info_t)&submap_info, &count);
    if (status_code != 0) {
      return status_code;
    }
    if (!submap_info.is_submap) {
      *protection = submap_info.protection;
      *user_tag = submap_info.user_tag;
      return 0;
    }
    ++*depth;
  }
}

internal kern_return_t
mac_dmn_compute_stack_range(
  mach_port_t task,
  mach_vm_address_t* address,
  mach_vm_size_t* size)
{ 
  natural_t depth = 9999999;
  vm_prot_t protection;
  unsigned int user_tag;
  
  // TODO(yuraiz): Check if the stack can consist from multiple segments.
  kern_return_t status_code =
  mac_dmn_vm_region_recurse_deepest(task, address, size, &depth, &protection, &user_tag);
  
  if(user_tag != VM_MEMORY_STACK)
  {
    *address = 0;
    *size = 0;
    return 1;
  }
  // NOTE(yuraiz): Not sure if that's exactly right, but it matches pthread's internal data.
  *size -= 512 - 32;
  return 0;
}

internal void
mac_dmn_process_update_dyld_notifier_addr(MAC_DMN_Process *process)
{
  // NOTE(yuraiz): At the initial stage the notification field inside all_image_infos isn't updated,
  // and is set by the compiler.
  //
  // So I compute the offset between the compile time known values, only masking away the high bits.
  // Not neccessary the second time, but still valid.

  struct task_dyld_info info = {0};
  mach_msg_type_number_t info_cnt = TASK_DYLD_INFO_COUNT;
  task_info(process->task, TASK_DYLD_INFO, (task_info_t)&info, &info_cnt);
  Assert(info.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_64);

  mach_vm_size_t read_count = 0;
  struct dyld_all_image_infos all_image_infos = {0};
  mach_vm_read_overwrite(process->task, info.all_image_info_addr, sizeof(all_image_infos),  (mach_vm_address_t)&all_image_infos, &read_count);

  //- yuraiz: compute the dyld notifier offset
  U64 mask48 = 0xFFFFFFFFFFFF;
  U64 base_dyld_all_image_infos_addr = IntFromPtr(all_image_infos.dyldAllImageInfosAddress) & mask48;
  U64 base_dyld_notification_addr = IntFromPtr(all_image_infos.notification) & mask48;
  U64 notification_offset = base_dyld_notification_addr - base_dyld_all_image_infos_addr;

  //- yuraiz: apply the offset to all image info
  process->ctx->dyld_notifier_address = IntFromPtr(info.all_image_info_addr) + notification_offset;
}

internal B32
mac_dmn_set_single_step_flag(MAC_DMN_Thread *thread, B32 is_on)
{
  B32 is_flag_set = 0;
  switch(thread->process->ctx->arch)
  {
    case Arch_Null: {} break;
    case Arch_arm64:
    {
      // Set SS (Single Stepping) bit
      if(is_on) { thread->debug_regs.mdscr_el1 |= 0x1;    }
      else      { thread->debug_regs.mdscr_el1 &= ~(0x1); }
      thread->is_reg_block_dirty = 1;
      is_flag_set = 1;
    } break;
    case Arch_x64:
    case Arch_x86:
    case Arch_arm32: { NotImplemented; }break;
    default: { InvalidPath; } break;
  }
  Assert(is_flag_set);
  return is_flag_set;
}

internal U64
mac_dmn_thread_read_ip(MAC_DMN_Thread *thread)
{
  ARCH_Info *arch_info = arch_info_from_arch(thread->process->ctx->arch);
  U64 result = arch_ip_from_reg_block(arch_info, thread->reg_block);
  return result;
}

internal U64
mac_dmn_thread_read_sp(MAC_DMN_Thread *thread)
{
  ARCH_Info *arch_info = arch_info_from_arch(thread->process->ctx->arch);
  U64 result = arch_sp_from_reg_block(arch_info, thread->reg_block);
  return result;
}

internal void
mac_dmn_thread_write_ip(MAC_DMN_Thread *thread, U64 ip)
{
  ARCH_Info *arch_info = arch_info_from_arch(thread->process->ctx->arch);
  arch_reg_block_write_ip(arch_info, thread->reg_block, ip);
  thread->is_reg_block_dirty = 1;
}

internal void
mac_dmn_thread_write_sp(MAC_DMN_Thread *thread, U64 sp)
{
  ARCH_Info *arch_info = arch_info_from_arch(thread->process->ctx->arch);
  arch_reg_block_write_sp(arch_info, thread->reg_block, sp);
  thread->is_reg_block_dirty = 1;
}

internal B32
mac_dmn_thread_read_reg_block(MAC_DMN_Thread *thread)
{
  if(thread != 0)
  {
    switch(thread->process->ctx->arch)
    {
      case Arch_Null: {} break;
      case Arch_arm64:
      {
        ARM64_RegBlock *reg_block = thread->reg_block;
        
        mach_msg_type_number_t count;

        arm_thread_state64_t thread_state = {0};
        count = ARM_THREAD_STATE64_COUNT;
        thread_get_state(thread->tid, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);
        arm_neon_state64_t neon_state = {0};
        count = ARM_NEON_STATE64_COUNT;
        thread_get_state(thread->tid, ARM_NEON_STATE64, (thread_state_t)&neon_state, &count);
        arm_debug_state64_t debug_state = {0};
        count = ARM_DEBUG_STATE64_COUNT;
        thread_get_state(thread->tid, ARM_DEBUG_STATE64, (thread_state_t)&debug_state, &count);
        // TODO(yuraiz): may be useful
        // arm_exception_state64_v2_t exception_state = {0};
        // count = ARM_EXCEPTION_STATE64_V2_COUNT;
        // thread_get_state(thread->tid, ARM_EXCEPTION_STATE64_V2, (thread_state_t)&exception_state, &count);

        // NOTE(yuraiz): Some of the registers are omitted in reg_block.

        MemoryCopy(&reg_block->x0, thread_state.__x, sizeof(thread_state.__x));
        reg_block->fp = thread_state.__fp;
        reg_block->lr = thread_state.__lr;
        reg_block->sp = thread_state.__sp;
        reg_block->pc = thread_state.__pc;

        MemoryCopy(&reg_block->v0, neon_state.__v, sizeof(neon_state.__v));

        MemoryCopy(&thread->debug_regs, &debug_state, sizeof(debug_state));
    
        return 1;
      } break;
      case Arch_x64:
      case Arch_x86:
      case Arch_arm32: { NotImplemented; }break;
      default: { InvalidPath; } break;
    }
  }
  return 0;
}

internal B32
mac_dmn_thread_write_reg_block(MAC_DMN_Thread *thread)
{
  switch(thread->process->ctx->arch)
  {
    case Arch_Null: {} break;
    case Arch_arm64:
    {
      ARM64_RegBlock *reg_block = thread->reg_block;
      
      mach_msg_type_number_t count;

      arm_thread_state64_t thread_state = {0};
      arm_neon_state64_t neon_state = {0};
      arm_debug_state64_t debug_state = {0};

      count = ARM_THREAD_STATE64_COUNT;
      thread_get_state(thread->tid, ARM_THREAD_STATE64, (thread_state_t)&thread_state, &count);
      count = ARM_NEON_STATE64_COUNT;
      thread_get_state(thread->tid, ARM_NEON_STATE64, (thread_state_t)&neon_state, &count);
      count = ARM_DEBUG_STATE64_COUNT;
      thread_get_state(thread->tid, ARM_DEBUG_STATE64, (thread_state_t)&debug_state, &count);

      MemoryCopy(thread_state.__x, &reg_block->x0, sizeof(thread_state.__x));
      thread_state.__fp = reg_block->fp;
      thread_state.__lr = reg_block->lr;
      thread_state.__sp = reg_block->sp;
      thread_state.__pc = reg_block->pc;
      
      MemoryCopy(neon_state.__v, &reg_block->v0, sizeof(neon_state.__v));

      MemoryCopy(&debug_state, &thread->debug_regs, sizeof(debug_state));
      
      count = ARM_THREAD_STATE64_COUNT;
      thread_set_state(thread->tid, ARM_THREAD_STATE64, (thread_state_t)&thread_state, count);
      count = ARM_NEON_STATE64_COUNT;
      thread_set_state(thread->tid, ARM_NEON_STATE64, (thread_state_t)&neon_state, count);
      count = ARM_DEBUG_STATE64_COUNT;
      thread_set_state(thread->tid, ARM_DEBUG_STATE64, (thread_state_t)&debug_state, count);

      return 1;
    } break;
    case Arch_x64:
    case Arch_x86:
    case Arch_arm32: { NotImplemented; }break;
    default: { InvalidPath; } break;
  }
  return 0;
}

internal U64
mac_dmn_process_read(MAC_DMN_Process* process, Rng1U64 range, void *dst)
{
  U64 result = 0;
  if(process)
  {
    mach_vm_address_t start = range.min;
    mach_vm_size_t to_read = dim_1u64(range);
    mach_vm_size_t readCount = 0;
    kern_return_t status_code = mach_vm_read_overwrite(process->task, start, to_read, (mach_vm_address_t)dst, &readCount);
    
    result = readCount;
  }
  return result;
}

internal B32
mac_dmn_process_write(MAC_DMN_Process* process, Rng1U64 range, void *src)
{
  U64 result = 0;
  if(process)
  {
    U64 to_write = dim_1u64(range);
    result = mac_dmn_write_to_protected(process->task, range.min, src, to_write, true);
  }
  return result != 0;
}

internal
STAP_MEMORY_READ(mac_dmn_stap_memory_read)
{
  MAC_DMN_Process *process = raw_ctx;
  U64 bytes_read = mac_dmn_process_read(process, r1u64(addr, addr + read_size), buffer);
  return bytes_read == read_size;
}

internal
MACHINE_OP_MEM_READ(mac_dmn_machine_op_mem_read)
{
  U64 read_size = mac_dmn_process_read((MAC_DMN_Process *)ud, r1u64(addr, addr + buffer_size), buffer);
  return read_size == buffer_size ? MachineOpResult_Ok : MachineOpResult_Fail;
}

internal B32
mac_dmn_mach_read_op_mem(U64 address, U64 size, void* dst, U64 src)
{
  U64 read_size = 0;
  mach_vm_read_overwrite(src, address, size, (mach_vm_address_t)dst, &read_size);
  return read_size == size;
}

internal String8
mac_dmn_process_lookup_symbol_name(Arena *arena, DMN_Handle process_handle, U64 vaddr)
{
  // TODO(yuraiz): reads from the process every time, and is called directly in eval code.
  // Make a normal abstraction with caching.

  MAC_DMN_Process *process = mac_dmn_process_from_handle(process_handle);
  if(process == 0 || process->ctx == 0) {return str8_zero();}

  Temp scratch = scratch_begin(&arena, 1);
  String8 result = {};

  //- yuraiz: find the module 
  MAC_DMN_Module *target_module = 0;
  for EachNode(module, MAC_DMN_Module, process->ctx->first_module)
  {
    if (vaddr >= module->base_vaddr && module->base_vaddr + module->size > vaddr)
    {
      target_module = module;
    }
  }

  if(target_module != 0)
  {
      //- yuraiz: read the symbol list
      struct nlist_64 *syms = push_array(scratch.arena, struct nlist_64, target_module->sym_count);
      mac_dmn_process_read(process, target_module->syms_range, syms);
      
      //- yuraiz: check if vaddr is for a stab -> read the symbol index from the symbol table
      U64 str_off = 0;
      if(vaddr >= target_module->stub_range.min && vaddr < target_module->stub_range.max)
      {
        U64 stub_idx = (vaddr - target_module->stub_range.min) / target_module->stub_size + target_module->stub_idx;
        if(stub_idx < target_module->dysymtab.nindirectsyms)
        {
          U32 stab_sym_idx = 0;
          U64 sym_idx_vaddr = target_module->dysymtab.indirectsymoff + stub_idx * sizeof(stab_sym_idx) +  target_module->base_vaddr;
          mac_dmn_process_read(process, rng_1u64(sym_idx_vaddr, sym_idx_vaddr + sizeof(stab_sym_idx)), &stab_sym_idx);
          str_off = syms[stab_sym_idx].n_un.n_strx;
        }
      }

      //- yuraiz: look up by voff in external symbols
      if(str_off == 0)
      {
        U64 target = vaddr - target_module->slide;
        U64 best_vaddr = 0;
        for EachIndex(idx, target_module->sym_count)
        {
          U32 strx = syms[idx].n_un.n_strx;
          U64 val = syms[idx].n_value;
          B32 has_str = strx != 0;

          if(has_str && val <= target && best_vaddr < val)
          {
            best_vaddr = val;
            str_off = strx;
          }
        }
      }

      if(str_off != 0)
      {
        U64 cap_string_size = 512;
        Rng1U64 str_range = target_module->symstr_range;
        str_range.min += str_off;
        str_range.max = Min(str_range.min + cap_string_size, str_range.max);

        char *str_buf = push_array(scratch.arena, char, dim_1u64(str_range));
        mac_dmn_process_read(process, str_range, str_buf);
        str_buf[dim_1u64(str_range) - 1] = 0;

        String8 string = str8_cstring_capped(str_buf, str_buf + dim_1u64(str_range));
        result = str8_copy(arena, string);

        {
          int status = 0;
          size_t len = 0;
          char *demangled = __cxa_demangle(str_buf, 0, &len, &status);

          if(status == 0)
          {
            result = str8_copy(arena, str8_cstring(demangled));
          }
          else
          {
            // strip the initial underscore to match the style of the other tools
            if(string.size > 0 && string.str[0] == '_')
            {
              string = str8_skip(string, 1);
            }
            result = str8_copy(arena, string);
          }

          free(demangled);
        }
      }
  }
  
  scratch_end(scratch);
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_entity_alloc(MAC_DMN_EntityKind kind)
{
  MAC_DMN_Entity *entity = mac_dmn_state->free_entity;
  if(entity != 0)
  {
    SLLStackPop(mac_dmn_state->free_entity);
  }
  else
  {
    entity = push_array(mac_dmn_state->entities_arena, MAC_DMN_Entity, 1);
    mac_dmn_state->entities_count += 1;
  }
  U32 gen = entity->gen;
  entity->gen += 1;
  U64 gen_mask = AlignOf(MAC_DMN_Entity) - 1;
  entity->gen &= gen_mask;
  entity->kind = kind;
  return entity;
}

internal MAC_DMN_Process *
mac_dmn_process_alloc(pid_t pid, MAC_DMN_ProcessState state, MAC_DMN_Process *parent_process, B32 debug_subprocesses, B32 is_cow)
{
  Temp scratch = scratch_begin(0, 0);
  
  MAC_DMN_Process *process = &mac_dmn_entity_alloc(MAC_DMN_EntityKind_Process)->process;
  process->pid                = pid;
  process->state              = state;
  process->debug_subprocesses = debug_subprocesses;
  process->is_cow             = is_cow;
  process->parent_process     = parent_process;
  
  kern_return_t status_code = task_for_pid(mach_task_self(), pid, &process->task);
  if(status_code != 0)
  {
    fprintf(stderr, "%%s: s", __func__, mach_error_string(status_code));
  }

  mac_dmn_subscribe_to_exceptions(process->task, mac_dmn_state->exc_port);
  
  // update pending process tracker
  if(state != MAC_DMN_ProcessState_Normal)
  {
    mac_dmn_state->process_pending_creation += 1;
  }
  
  // add process to the list
  DLLPushBack(mac_dmn_state->first_process, mac_dmn_state->last_process, process);
  mac_dmn_state->process_count += 1;
  
  // push pid -> MAC_DMN_Process mapping
  hash_table_push_u64_raw(mac_dmn_state->arena, mac_dmn_state->pid_ht, pid, process);
  
  scratch_end(scratch);
  return process;
}

internal MAC_DMN_Module *
mac_dmn_module_alloc(MAC_DMN_Process *process, U64 load_address, U64 name_vaddr)
{
  Temp scratch = scratch_begin(0, 0);
  MAC_DMN_Module *module = &mac_dmn_entity_alloc(MAC_DMN_EntityKind_Module)->module;

  // TODO(yuraiz): I guess we can read more useful info from here
  MACH_Bin info = mach_extract_file_info(scratch.arena, load_address, mac_dmn_mach_read_op_mem, process->task);
  
  DLLPushBack(process->ctx->first_module, process->ctx->last_module, module);
  process->ctx->module_count += 1;

  // NOTE(yuraiz): That address expected to have the offset in some places,
  // but in other's it should be just load_address.
  // That offset ctrl_thread__module_open magic detection, but is required for the correct symbol mapping.
  S64 pref_load_address  = mach_compute_pref_load_address(info);
  S64 slide = load_address - pref_load_address;

  module->base_vaddr     = load_address;
  module->name_vaddr     = name_vaddr;
  module->slide          = slide;
  module->size           = mach_compute_image_size(info);
  module->guid           = mach_get_uuid(info);
  
  module->unwind_info_range = mach_find_unwind_info(info);
  if(module->unwind_info_range.min != 0)
  {
    module->unwind_info_range.min += slide;
    module->unwind_info_range.max += slide;
  }

  module->eh_frame_range = mach_find_eh_frame(info);
  if(module->eh_frame_range.min != 0)
  {
    module->eh_frame_range.min += slide;
    module->eh_frame_range.max += slide;
  }
  // module->eh_frame_range = rng_1u64(0, 0);

  struct section_64 stub_section = mach_get_section_64(info,  s("__TEXT"), s("__stubs"));
  module->stub_size = stub_section.reserved2;
  module->stub_idx = stub_section.reserved1;
  module->stub_range = r1u64(stub_section.addr, stub_section.addr + stub_section.size);

  if(module->stub_range.min != 0)
  {
    module->stub_range.min += slide;
    module->stub_range.max += slide;
  }

  S64 linkedit_slide = 0;
  U8 *command_buf = info.buf;
  for EachIndex(cmd_idx, info.command_count)
  {
    struct load_command *ld_cmd = (struct load_command *)command_buf;

    if (ld_cmd->cmd == LC_SEGMENT_64)
    {
      struct segment_command_64 *command = (struct segment_command_64 *)ld_cmd;
      String8 segname = str8_cstring(command->segname);
      if(str8_match_lit("__LINKEDIT", segname, 0))
      {
        S64 linkedit_file_offset = command->fileoff;
        S64 linkedit_vaddr = command->vmaddr + slide;
        linkedit_slide = linkedit_vaddr - linkedit_file_offset;
      }
    }

    command_buf += ld_cmd->cmdsize;
  }

  //- yuraiz: extract symbol info ranges
  command_buf = info.buf;
  for EachIndex(cmd_idx, info.command_count)
  {
    struct load_command *ld_cmd = (struct load_command *)command_buf;
    if(ld_cmd->cmd == LC_SYMTAB)
    {
      struct symtab_command *command = (struct symtab_command *)ld_cmd;

      // TODO(yuraiz): filter out symbols

      mach_vm_address_t sym_addr = linkedit_slide + command->symoff;
      mach_vm_size_t sym_size = sizeof(struct nlist_64) * command->nsyms;
      module->syms_range = rng_1u64(sym_addr, sym_addr + sym_size);
      module->sym_count = command->nsyms;

      mach_vm_address_t str_addr = linkedit_slide + command->stroff;
      module->symstr_range = rng_1u64(str_addr, str_addr + command->strsize);
    }
    if(ld_cmd->cmd == LC_DYSYMTAB)
    {
      module->dysymtab = *(struct dysymtab_command *)ld_cmd;
      module->dysymtab.indirectsymoff += linkedit_slide;
    }

    command_buf += ld_cmd->cmdsize;
  }

  scratch_end(scratch);
  return module;
}

internal MAC_DMN_ProcessCtx *
mac_dmn_process_ctx_alloc(MAC_DMN_Process *process, B32 is_rebased)
{
  MAC_DMN_ProcessCtx *ctx = &mac_dmn_entity_alloc(MAC_DMN_EntityKind_ProcessCtx)->process_ctx;

  ctx->arena             = arena_alloc();
  ctx->arch              = Arch_arm64;
  ctx->loaded_modules_ht = hash_table_init(ctx->arena, 0x1000);
  
  return ctx;
}

internal B32
mac_dmn_hardware_breakpoint(MAC_DMN_Thread *thread, U8 id, U64 address, B32 enable, B32 single_step)
{
  mach_msg_type_number_t count;
  arm_debug_state64_t debug_state = {0};
  count = ARM_DEBUG_STATE64_COUNT;
  thread_get_state(thread->tid, ARM_DEBUG_STATE64, (thread_state_t)&debug_state, &count);

  // Set the breakpoint address.
  debug_state.__bvr[id] = address;
  // Enable the breakpoint.
  debug_state.__bcr[id] = enable ? 0x1e5 : 0;

  B32 was_single_step = debug_state.__mdscr_el1 & (U32)0x1; 

  // Set SS (Single Stepping) bit
  if(single_step) { debug_state.__mdscr_el1 |= (U32)0x1;    }
  else            { debug_state.__mdscr_el1 &= ~((U32)0x1); }

  count = ARM_DEBUG_STATE64_COUNT;
  thread_set_state(thread->tid, ARM_DEBUG_STATE64, (thread_state_t)&debug_state, count);

  return !was_single_step && single_step;
}

internal void
mac_dmn_thread_set_probes(MAC_DMN_Thread *thread)
{
  // NOTE(yuraiz): Currently we set only a singe trap.
  Temp scratch = scratch_begin(0, 0);

  U64 breakpoint_location = thread->process->ctx->dyld_notifier_address;

  mac_dmn_hardware_breakpoint(thread, 0, breakpoint_location, 1, 0);

  scratch_end(scratch);
}

internal MAC_DMN_Thread *
mac_dmn_thread_alloc(MAC_DMN_Process *process, MAC_DMN_ThreadState thread_state, thread_act_t tid)
{
  void *reg_block;
  if(process->ctx->free_reg_blocks.node_count)
  {
    String8Node *n = str8_list_pop_front(&process->ctx->free_reg_blocks);
    reg_block = n->string.str;
    str8_list_push_node(&process->ctx->free_reg_block_nodes, n);
  }
  else
  {
    ARCH_Info *arch_info = arch_info_from_arch(process->ctx->arch);
    U64 reg_block_size = arch_info->reg_block_size;
    reg_block = push_array(process->ctx->arena, U8, reg_block_size);
  }
  
  MAC_DMN_Thread *thread = &mac_dmn_entity_alloc(MAC_DMN_EntityKind_Thread)->thread;
  thread->tid       = tid;
  thread->state     = thread_state;
  thread->process   = process;
  thread->reg_block = reg_block;

  mac_dmn_thread_set_probes(thread);

  // NOTE(yuraiz): It's safe to read the registers even if the thread isn't suspended.
  thread->is_reg_block_dirty = !mac_dmn_thread_read_reg_block(thread);
  
  struct thread_identifier_info identifier_info;
  mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
  kern_return_t kr = thread_info(thread->tid,
                                 THREAD_IDENTIFIER_INFO,
                                 (thread_info_t)&identifier_info,
                                 &count);
  // NOTE(yuraiz): Don't know yet if it's even close, but it points to pthread_s->tsd[]
  thread->thread_local_base = identifier_info.thread_handle;
  // TODO(yuraiz): use identifier_info->thread_id
  
  // compute stack bounds
  U64 stack_pointer = mac_dmn_thread_read_sp(thread);
  mach_vm_size_t stack_size = 0;
  mac_dmn_compute_stack_range(thread->process->task, &stack_pointer, &stack_size);

  thread->stackaddr = stack_pointer;
  thread->stackbottom = stack_pointer - stack_size;

  // add thread to the list
  DLLPushBack(process->first_thread, process->last_thread, thread);
  process->thread_count += 1;
  
  // push tid -> thread mapping
  hash_table_push_u64_raw(mac_dmn_state->arena, mac_dmn_state->tid_ht, thread->tid, thread);
  
  // update global thread counter
  if(thread_state == MAC_DMN_ThreadState_PendingCreation)
  {
    mac_dmn_state->threads_pending_creation += 1;
  }
  
  return thread;
}

internal void
mac_dmn_entity_release(MAC_DMN_Entity *entity)
{
  U32 gen = entity->gen + 1;
  MemoryZeroStruct(entity);
  entity->gen = gen;
  SLLStackPush(mac_dmn_state->free_entity, entity);
}

internal void
mac_dmn_process_release(MAC_DMN_Process *process)
{
  // update global state
  AssertAlways(mac_dmn_state->process_count > 0);
  DLLRemove(mac_dmn_state->first_process, mac_dmn_state->last_process, process);
  mac_dmn_state->process_count -= 1;
  
  // update pending process tracker
  if(process->state != MAC_DMN_ProcessState_Normal)
  {
    Assert(mac_dmn_state->process_pending_creation > 0);
    mac_dmn_state->process_pending_creation -= 1;
  }
  
  // remove pid mapping
  hash_table_purge_u64(mac_dmn_state->pid_ht, process->pid);
  
  // release the context
  if(process->ctx)
  {
    // TODO(yuraiz)
    // mac_dmn_process_ctx_release(process->ctx);
  }
  
  // release process entity
  mac_dmn_entity_release((MAC_DMN_Entity *)process);
}

internal void
mac_dmn_process_ctx_release(MAC_DMN_ProcessCtx *ctx)
{
  Assert(ctx->ref_count > 0);
  ctx->ref_count -= 1;
  
  if(ctx->ref_count == 0)
  {
    arena_release(ctx->arena);
    mac_dmn_entity_release((MAC_DMN_Entity *)ctx);
  }
}

internal void
mac_dmn_thread_release(MAC_DMN_Thread *thread)
{
  MAC_DMN_Process *process = thread->process;
  
  // purge tid mapping
  hash_table_purge_u64(mac_dmn_state->tid_ht, thread->tid);
  
  // update global thread counter
  if(thread->state == MAC_DMN_ThreadState_PendingCreation)
  {
    AssertAlways(mac_dmn_state->threads_pending_creation > 0);
    mac_dmn_state->threads_pending_creation -= 1;
  }
  
  // remove thread from the list
  Assert(process->thread_count > 0);
  DLLRemove(process->first_thread, process->last_thread, thread);
  process->thread_count -= 1;
  
  // push reg block to the free list
  String8Node *reg_block_node;
  if(process->ctx->free_reg_block_nodes.node_count)
  {
    reg_block_node = str8_list_pop_front(&process->ctx->free_reg_block_nodes);
  }
  else
  {
    reg_block_node = push_array(process->ctx->arena, String8Node, 1);
  }
  reg_block_node->string = str8(thread->reg_block, 0);
  str8_list_push_node(&process->ctx->free_reg_blocks, reg_block_node);
  
  mac_dmn_entity_release((MAC_DMN_Entity *)thread);
}

internal void
mac_dmn_module_release(MAC_DMN_ProcessCtx *ctx, MAC_DMN_Module *module)
{
  // remove module from the list
  Assert(ctx->module_count > 0);
  DLLRemove(ctx->first_module, ctx->last_module, module);
  ctx->module_count -= 1;
  
  // purge base addr -> module mapping
  hash_table_purge_u64(ctx->loaded_modules_ht, module->base_vaddr);
  
  mac_dmn_entity_release((MAC_DMN_Entity *)module);
}

internal DMN_Handle
mac_dmn_handle_from_entity(MAC_DMN_Entity *entity)
{
  DMN_Handle handle = {0};
  U64 index = IntFromPtr(entity);
  handle.u64[0] = index;

  U64 gen_mask = AlignOf(MAC_DMN_Entity) - 1;
  
  //-yuraiz assert that no meaningful bits are cleared
  Assert(((handle.u64[0] & gen_mask) == 0) && "Invalid gen mask");
  handle.u64[0] |= entity->gen & gen_mask;

  return handle;
}

internal DMN_Handle
mac_dmn_handle_from_process(MAC_DMN_Process *process)
{
  return mac_dmn_handle_from_entity((MAC_DMN_Entity *)process);
}

internal DMN_Handle
mac_dmn_handle_from_process_ctx(MAC_DMN_ProcessCtx *process_ctx)
{
  return mac_dmn_handle_from_entity((MAC_DMN_Entity *)process_ctx);
}

internal DMN_Handle
mac_dmn_handle_from_thread(MAC_DMN_Thread *thread)
{
  return mac_dmn_handle_from_entity((MAC_DMN_Entity *)thread);
}

internal DMN_Handle
mac_dmn_handle_from_module(MAC_DMN_Module *module)
{
  return mac_dmn_handle_from_entity((MAC_DMN_Entity *)module);
}

internal MAC_DMN_Entity *
mac_dmn_entity_from_handle(DMN_Handle handle, MAC_DMN_EntityKind expected_kind)
{
  // NOTE(yuraiz): I had crashes with the implementation using offset from the
  // base so I decided to use the lowest bits to store the generation.
  U64 gen_mask = AlignOf(MAC_DMN_Entity) - 1;
  U64 pointer_value = handle.u64[0] & ~(gen_mask);
  U64 gen_value = handle.u64[0] & gen_mask;
  MAC_DMN_Entity *result = PtrFromInt(pointer_value);

  if(result != 0)
  {
    if((result->gen & gen_mask) != gen_value)
    {
      result = 0;
    }
    else if(result->kind != expected_kind)
    {
      result = 0;
    }
  }
  return result;
}

internal MAC_DMN_Process *
mac_dmn_process_from_handle(DMN_Handle process_handle)
{
  return (MAC_DMN_Process *)mac_dmn_entity_from_handle(process_handle, MAC_DMN_EntityKind_Process);
}

internal MAC_DMN_ProcessCtx *
mac_dmn_process_ctx_from_handle(DMN_Handle process_ctx_handle)
{
  return (MAC_DMN_ProcessCtx *)mac_dmn_entity_from_handle(process_ctx_handle, MAC_DMN_EntityKind_ProcessCtx);
}

internal MAC_DMN_Thread *
mac_dmn_thread_from_handle(DMN_Handle thread_handle)
{
  return (MAC_DMN_Thread *)mac_dmn_entity_from_handle(thread_handle, MAC_DMN_EntityKind_Thread);
}

internal MAC_DMN_Module *
mac_dmn_module_from_handle(DMN_Handle module_handle)
{
  return (MAC_DMN_Module *)mac_dmn_entity_from_handle(module_handle, MAC_DMN_EntityKind_Module);
}

internal MAC_DMN_Thread *
mac_dmn_thread_from_pid(pid_t tid)
{
  return hash_table_search_u64_raw(mac_dmn_state->tid_ht, tid);
}

internal MAC_DMN_Process *
mac_dmn_process_from_pid(pid_t pid)
{
  return hash_table_search_u64_raw(mac_dmn_state->pid_ht, pid);
}

// event helpers

internal void
mac_dmn_push_event_create_process(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process)
{
  Temp scratch = scratch_begin(&arena, 1);
  // push create process event
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind      = DMN_EventKind_CreateProcess;
  e->process   = mac_dmn_handle_from_process(process);
  e->arch      = process->ctx->arch;
  e->code      = process->pid;
  // e->tls_model = DMN_TlsModel_MacOS;

  scratch_end(scratch);
}

internal void
mac_dmn_push_event_exit_process(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_ExitProcess;
  e->process = mac_dmn_handle_from_process(process);
  e->code    = process->main_thread_exit_code;
}

internal void
mac_dmn_push_event_create_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_CreateThread;
  e->process             = mac_dmn_handle_from_process(thread->process);
  e->thread              = mac_dmn_handle_from_thread(thread);
  e->arch                = thread->process->ctx->arch;
  e->code                = thread->tid;
  e->instruction_pointer = mac_dmn_thread_read_ip(thread);

  struct thread_extended_info info;
  mach_msg_type_number_t info_cnt = THREAD_EXTENDED_INFO_COUNT;
  kern_return_t status_code = thread_info(thread->tid, THREAD_EXTENDED_INFO, (thread_info_t)&info, &info_cnt);
  if(info.pth_name[0] != 0)
  {
    e = dmn_event_list_push(arena, events);
    e->kind    = DMN_EventKind_SetThreadName;
    e->process = mac_dmn_handle_from_process(thread->process);
    e->thread  = mac_dmn_handle_from_thread(thread);

    e->string  = str8_copy(arena, str8_cstring(info.pth_name));
    e->code    = thread->tid;
  }
}

internal void
mac_dmn_push_event_exit_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread, U64 exit_code)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_ExitThread;
  e->process = mac_dmn_handle_from_process(thread->process);
  e->thread  = mac_dmn_handle_from_thread(thread);
  e->code    = exit_code;
}

internal void
mac_dmn_push_event_load_module(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, MAC_DMN_Module *module)
{
  Temp scratch = scratch_begin(&arena, 1);

  char* path_buffer = push_array(scratch.arena, char, PATH_MAX);

  // NOTE(yuraz): The path may be at the end of the readable region
  U64 to_read = PATH_MAX;
  for(U64 bytes_read = 0;to_read > 0 && bytes_read != to_read; to_read--)
  {
    bytes_read = mac_dmn_process_read(process, rng_1u64(module->name_vaddr, module->name_vaddr + to_read), path_buffer);
  }

  String8 path = str8_copy(arena, str8_cstring(path_buffer));

  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind            = DMN_EventKind_LoadModule;
  e->process         = mac_dmn_handle_from_process(process);
  e->module          = mac_dmn_handle_from_module(module);
  e->arch            = process->ctx->arch;
  e->address         = module->base_vaddr;
  e->string          = push_str8_copy(arena, path);
  e->size            = module->size;

  // TODO(yuraiz): Fill the module info
  
  DMN_ModuleInfo *module_info = push_array(arena, DMN_ModuleInfo, 1);

  module_info->arch = process->ctx->arch;
  module_info->vsize = module->size;

  // rjf: entry point
  // module_info->entry_point_voff;
  
  // TODO(yuraiz): Use more advanced lookup
  String8 debug_info_path =str8f(arena, "%S.dSYM/Contents/Resources/DWARF/%S", path, str8_skip_last_slash(path));

  module_info->module_path = push_str8_copy(arena, path);

  // rjf: debug info key
  module_info->debug_info_path = debug_info_path;
  module_info->debug_info_guid = module->guid;
  // U64 debug_info_timestamp;
  // U64 debug_info_age;

  module_info->compact_unwind_vaddr_range = module->unwind_info_range;

  module_info->eh_frame_header_vaddr_range = module->eh_frame_range;

  // // rjf: thread-local storage info
  // U64 tls_index;
  // U64 tls_offset;
  
  // // rjf: unwinding info
  // Rng1U64 pe_intel_pdatas_vaddr_range;
  // Rng1U64 eh_frame_header_vaddr_range;
  
  // // rjf: special raddbg data
  // Rng1U64 raddbg_info_voff_range;
  // U64 raddbg_is_attached_marker_voff;

  e->module_info     = module_info;

  scratch_end(scratch);
}

internal void
mac_dmn_push_event_unload_module(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, MAC_DMN_Module *module)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_UnloadModule;
  e->process = mac_dmn_handle_from_process(process);
  e->module  = mac_dmn_handle_from_module(module);
}

internal void
mac_dmn_push_event_handshake_complete(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_HandshakeComplete;
  e->process = mac_dmn_handle_from_process(process);
  e->thread  = mac_dmn_handle_from_thread(process->first_thread);
  e->arch    = process->ctx->arch;
}

internal void
mac_dmn_push_event_breakpoint(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread, U64 address)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_Breakpoint;
  e->process             = mac_dmn_handle_from_process(thread->process);
  e->thread              = mac_dmn_handle_from_thread(thread);
  e->instruction_pointer = mac_dmn_thread_read_ip(thread);
  // TODO(yuraiz): Figure out when to set that
  e->address             = address;
}

internal void
mac_dmn_push_event_single_step(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_SingleStep;
  e->process             = mac_dmn_handle_from_process(thread->process);
  e->thread              = mac_dmn_handle_from_thread(thread);
  e->instruction_pointer = mac_dmn_thread_read_ip(thread);
  e->address             = e->instruction_pointer;
}

internal void
mac_dmn_push_event_exception(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread, U64 signo)
{
  local_persist B8 is_repeatable[] =
  {
    0, // null
    [SIGHUP] = 1,
    [SIGINT] = 1,
    [SIGQUIT] = 1,
    [SIGTRAP] = 1,
    [SIGABRT] = 1,
    [SIGIOT] = 1,
    [SIGBUS] = 1,
    [SIGFPE] = 1,
    [SIGKILL] = 1,
    [SIGUSR1] = 1,
    [SIGSEGV] = 1,
    [SIGUSR2] = 1,
    [SIGPIPE] = 1,
    [SIGALRM] = 1,
    [SIGTERM] = 1,
    [SIGCHLD] = 0,
    [SIGCONT] = 0,
    [SIGSTOP] = 1,
    [SIGTTIN] = 1,
    [SIGTTOU] = 1,
    [SIGURG] = 0,
    [SIGXCPU] = 1,
    [SIGXFSZ] = 1,
    [SIGVTALRM] = 1,
    [SIGPROF] = 1,
    [SIGWINCH] = 0,
    [SIGIO] = 1,
    [SIGSYS] = 1,
    // TODO(yuraiz): define more of the signals here
  };
  
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_Exception;
  e->process             = mac_dmn_handle_from_process(thread->process);
  e->thread              = mac_dmn_handle_from_thread(thread);
  e->instruction_pointer = mac_dmn_thread_read_ip(thread);
  e->code                = signo;
  e->exception_repeated  = signo < ArrayCount(is_repeatable) ? is_repeatable[signo] : 0;
  
  if(signo == SIGSEGV)
  {
    siginfo_t si = {0};
    // TODO(yuraiz)
    // OS_LNX_RETRY_ON_EINTR(ptrace(PTRACE_GETSIGINFO, thread->tid, 0, &si));
    e->address = (U64)si.si_addr;
  }
}

internal void
mac_dmn_push_event_halt(Arena *arena, DMN_EventList *events)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_Halt;
}

internal void
mac_dmn_push_event_not_attached(Arena *arena, DMN_EventList *events)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind       = DMN_EventKind_Error;
  e->error_kind = DMN_ErrorKind_NotAttached;
}

internal MAC_DMN_Thread *
mac_dmn_event_create_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, thread_act_t tid)
{
  MAC_DMN_Thread *thread = mac_dmn_thread_alloc(process, MAC_DMN_ThreadState_Stopped, tid);
  mac_dmn_push_event_create_thread(arena, events, thread);
  return thread;
}

internal void
mac_dmn_event_exit_thread(Arena *arena, DMN_EventList *events, pid_t tid, U64 exit_code)
{
  MAC_DMN_Thread  *thread  = mac_dmn_thread_from_pid(tid);
  MAC_DMN_Process *process = thread->process;
  
  // store main thread's exit code
  if(thread->tid == thread->process->pid)
  {
    thread->process->main_thread_exit_code = exit_code;
  }
  
  // push exit event
  mac_dmn_push_event_exit_thread(arena, events, thread, exit_code);
  
  // release entity
  mac_dmn_thread_release(thread);
  
  // auto exit process on last thread
  if(process->thread_count == 0)
  {
    mac_dmn_event_exit_process(arena, events, process->pid);
  }
}

internal MAC_DMN_Process *
mac_dmn_event_create_process(Arena *arena, DMN_EventList *events, pid_t pid, MAC_DMN_Process *parent_process, MAC_DMN_CreateProcessFlags flags)
{
  MAC_DMN_Process *process = mac_dmn_process_alloc(pid, MAC_DMN_ProcessState_Normal, parent_process, !!(flags & MAC_DMN_CreateProcessFlag_DebugSubprocesses), !!(flags & MAC_DMN_CreateProcessFlag_Cow));
  
  if(flags & MAC_DMN_CreateProcessFlag_ClonedMemory)
  {
    process->ctx = parent_process->ctx;
  }
  else
  {
    process->ctx = mac_dmn_process_ctx_alloc(process, !!(flags & MAC_DMN_CreateProcessFlag_Rebased));
  }
  process->ctx->ref_count += 1;
    
  // push events
  mac_dmn_push_event_create_process(arena, events, process);

  return process;
}

internal void
mac_dmn_event_exit_process(Arena *arena, DMN_EventList *events, pid_t pid)
{
  MAC_DMN_Process *process = mac_dmn_process_from_pid(pid);
  AssertAlways(process->thread_count == 0);
  
  // push module events
  for EachNode(module, MAC_DMN_Module, process->ctx->first_module)
  {
    mac_dmn_push_event_unload_module(arena, events, process, module);
  }
  
  // push process exit event
  mac_dmn_push_event_exit_process(arena, events, process);

  // release process
  mac_dmn_process_release(process);
}

internal void
mac_dmn_event_load_module(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, U64 load_address, U64 name_vaddr)
{
  MAC_DMN_Module *module = mac_dmn_module_alloc(process, load_address, name_vaddr);
  mac_dmn_push_event_load_module(arena, events, process, module);
}

internal void
mac_dmn_event_unload_module(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, MAC_DMN_Module *module)
{
  mac_dmn_push_event_unload_module(arena, events, process, module);
  mac_dmn_module_release(process->ctx, module);
}

internal B32
mac_dmn_event_probe_breakpoint(Arena* arena, DMN_EventList *events, MAC_DMN_Thread *thread, U64 address)
{
  Temp scratch = scratch_begin(&arena, 1);
  B32 result = 0;

  MAC_DMN_Process *process = thread->process;
  if(address == process->ctx->dyld_notifier_address)
  {
    //////////////////////////////
    // NOTE(yuraiz): On dyld notifier.
    // 
    // macOS, unlike Windows, doesn't send any events when modules are loaded or unloaded.
    // Debuggers discover that by setting a breakpoint on a special dyld_image_notifier function.
    // 
    // You can find it in the dyld_all_image_infos.notifier field, but it isn't valid just after the start,
    // so I compute it by the offset to the dyld_all_image_infos which I compute from dyld_all_image_infos too.

    // disable the breakpoint for a single step
    thread->clear_single_step = mac_dmn_hardware_breakpoint(thread, 0, process->ctx->dyld_notifier_address, 0, 1); 
    thread->hit_hardware_breakpoint = 1;

    // NOTE(yuraiz): don't actually update the registers
    ARM64_RegBlock saved_regs = *(ARM64_RegBlock *)thread->reg_block;
    mac_dmn_thread_read_reg_block(thread);
    ARM64_RegBlock reg_block = *(ARM64_RegBlock *)thread->reg_block;
    *((ARM64_RegBlock *)thread->reg_block) = saved_regs;

    // read the arguments of dyld_image_notifier.
    // TODO(yuraiz): verify that we get the correct values here
    enum dyld_image_mode mode = reg_block.x0;
    U32 info_count = reg_block.x1;
    U64 info_addr = reg_block.x2;

    mach_vm_size_t read_count = 0;
    struct dyld_image_info *image_info_array = push_array(arena, struct dyld_image_info, info_count);
    mach_vm_size_t array_size = sizeof(struct dyld_image_info) * info_count;

    mach_vm_read_overwrite(process->task, info_addr, array_size, (mach_vm_address_t)image_info_array, &read_count);

    switch(mode)
    {
      case dyld_image_adding:
      {
        B32 is_main_module = process->ctx->first_module == 0 && process->dyld_move_vaddr != 0;

        // NOTE(yuraiz): For some reason dyld notifies about the main module multiple times.
        // Maybe we can compare only with the main module

        // generate load module events
        for EachIndex(i, info_count)
        {
          U64 load_address = (U64)image_info_array[i].imageLoadAddress;
          U64 name_vaddr   = (U64)image_info_array[i].imageFilePath;
          B32 exists = 0;
          for EachNode(module, MAC_DMN_Module, process->ctx != 0 ? process->ctx->first_module : 0)
          {
            if(module->base_vaddr == load_address)
            {
              exists = 1;
            }
          }
          if(!exists)
          {
            mac_dmn_event_load_module(arena, events, process, load_address, name_vaddr);
          }
        }

        // the process, the main thread, and the main module are now created
        if(is_main_module)
        {
          mac_dmn_push_event_handshake_complete(arena, events, process);

          // load dyld module
          mac_dmn_event_load_module(arena, events, process, process->dyld_move_vaddr, process->dyld_name_vaddr);
        }
      }break;
      case dyld_image_removing:
      {
        // generate unload module events
        for EachNode(module, MAC_DMN_Module, process->ctx->first_module)
        {
          for EachIndex(i, info_count)
          {
            U64 load_address = (U64)image_info_array[i].imageLoadAddress;
            if(module->base_vaddr == load_address)
            {
              mac_dmn_event_unload_module(arena, events, process, module);
            }
          }
        }
      }break;
      case dyld_image_info_change:
      {
        // generate unload module events
        for EachNode(module, MAC_DMN_Module, process->ctx->first_module)
        {
          B32 exists = 0;
          for EachIndex(i, info_count)
          {
            U64 load_address = (U64)image_info_array[i].imageLoadAddress;
            if(module->base_vaddr == load_address)
            {
              exists = 1;
            }
          }
          if(!exists)
          {
            mac_dmn_event_unload_module(arena, events, process, module);
          }
        }

        // generate load module events
        for EachIndex(i, info_count)
        {
          U64 load_address = (U64)image_info_array[i].imageLoadAddress;
          U64 name_vaddr   = (U64)image_info_array[i].imageFilePath;

          B32 exists = 0;
          for EachNode(module, MAC_DMN_Module, process->ctx->first_module)
          {
            if(module->base_vaddr == load_address)
            {
              exists = 1;
            }
          }

          if(!exists)
          {
            mac_dmn_event_load_module(arena, events, process, load_address, name_vaddr);
          }
        }
      }break;
      case dyld_image_dyld_moved:
      {
        // dyld moved -> we need to read the new notifier address
        mac_dmn_process_update_dyld_notifier_addr(process);

        for EachNode(module, MAC_DMN_Module, process->ctx->first_module)
        {
          mac_dmn_event_unload_module(arena, events, process, module);
        }

        // NOTE(yuraiz): the debugger expects the first module to be the main one,
        // so postpond notifying about dyld until the actual main module is loaded.
        process->dyld_move_vaddr = (U64)image_info_array[0].imageLoadAddress;
        process->dyld_name_vaddr = (U64)image_info_array[0].imageFilePath;
      }break;
    }
    result = 1;
  }
  else if(thread->hit_hardware_breakpoint)
  {
    // turn the breakpoint back on and disable single step if needed
    mac_dmn_hardware_breakpoint(thread, 0, process->ctx->dyld_notifier_address, 1, !thread->clear_single_step); 
    thread->hit_hardware_breakpoint = 0;
    thread->clear_single_step = 0;
    result = 1;
  }

  scratch_end(scratch);

  return result;
}

internal void
mac_dmn_event_breakpoint(Arena *arena, DMN_EventList *events, MAC_DMN_ActiveTrap *user_traps, pid_t tid)
{
  MAC_DMN_Thread  *thread  = mac_dmn_thread_from_pid(tid);
  U64              ip      = mac_dmn_thread_read_ip(thread);

  DMN_Handle process = mac_dmn_handle_from_process(thread->process);

  DMN_Trap *hit_user_trap = 0;
  for EachNode(mac_trap, MAC_DMN_ActiveTrap, user_traps)
  {
    DMN_Trap *trap = mac_trap->trap;
    if(dmn_handle_match(trap->process, process))
    {
      if(trap->flags == 0 && dmn_handle_match(trap->process, process) && trap->vaddr == ip)
      {
        hit_user_trap = trap;
        break;
      }
    }
  }

  // rjf: generate event
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_Breakpoint;
  // e->kind                = hit_user_trap ? DMN_EventKind_Breakpoint : DMN_EventKind_Trap;
  e->process             = process;
  e->thread              = mac_dmn_handle_from_thread(thread);
  e->instruction_pointer = ip;
  // e->user_data           = hit_user_trap ? hit_user_trap->id : 0;
}

internal void
mac_dmn_event_data_breakpoint(Arena *arena, DMN_EventList *events, pid_t tid)
{
  MAC_DMN_Thread *thread = mac_dmn_thread_from_pid(tid);
  
  B32 is_valid = 1;
  U64 address  = 0;
  switch(thread->process->ctx->arch)
  {
    case Arch_Null: {} break;
    case Arch_x86:
    case Arch_x64:
    case Arch_arm32:
    case Arch_arm64:
    { NotImplemented; } break;
    default: { InvalidPath; } break;
  }
  
  if(is_valid)
  {
    mac_dmn_push_event_breakpoint(arena, events, thread, address);
  }
}

internal void
mac_dmn_event_halt(Arena *arena, DMN_EventList *events)
{
  mac_dmn_push_event_halt(arena, events);
}

internal void
mac_dmn_event_single_step(Arena *arena, DMN_EventList *events, pid_t tid)
{
  MAC_DMN_Thread *thread = mac_dmn_thread_from_pid(tid);
  
  // clear single step flag
  mac_dmn_set_single_step_flag(thread, 0);
  
  // push event
  mac_dmn_push_event_single_step(arena, events, thread);
}

internal void
mac_dmn_event_exception(Arena *arena, DMN_EventList *events, pid_t tid, U64 signo)
{
  MAC_DMN_Thread *thread = mac_dmn_thread_from_pid(tid);
  
  thread->pass_through_signal = 1;
  thread->pass_through_signo  = signo;
  
  mac_dmn_push_event_exception(arena, events, thread, signo);
}

internal MAC_DMN_Process *
mac_dmn_event_attach(Arena *arena, DMN_EventList *events, pid_t pid)
{
  Temp scratch = scratch_begin(&arena, 1);
  
  // create process
  MAC_DMN_Process *process = mac_dmn_event_create_process(arena, events, pid, 0, MAC_DMN_CreateProcessFlag_DebugSubprocesses|MAC_DMN_CreateProcessFlag_Rebased);
  
  // handshake complete
  mac_dmn_push_event_handshake_complete(arena, events, process);
  
  scratch_end(scratch);
  return process;
}

////////////////////////////////
//~ rjf: @dmn_os_hooks Main Layer Initialization (Implemented Per-OS)

internal void
dmn_init(void)
{
  if(mac_dmn_state == 0)
  {
    local_persist MAC_DMN_State state;
    mac_dmn_state = &state;
    
    mac_dmn_state->arena          = arena_alloc();
    mac_dmn_state->access_mutex   = mutex_alloc();
    mac_dmn_state->entities_arena = arena_alloc(.reserve_size = GB(32), .commit_size = KB(64), .flags = ArenaFlag_NoChain);
    mac_dmn_state->entities_base  = push_array(mac_dmn_state->entities_arena, MAC_DMN_Entity, 0);
    mac_dmn_state->tid_ht         = hash_table_init(mac_dmn_state->arena, 0x2000);
    mac_dmn_state->pid_ht         = hash_table_init(mac_dmn_state->arena, 0x400);
    mac_dmn_state->halter_mutex   = mutex_alloc();
    mac_dmn_entity_alloc(MAC_DMN_EntityKind_Null);

    mac_dmn_state->exc_port = mac_dmn_make_exception_port();
    
  }
  if(mac_dmn_exception_state == 0)
  {
    local_persist MAC_DMN_ExceptionState state;
    mac_dmn_exception_state = &state;
    
    mac_dmn_exception_state->arena = arena_alloc();
  }
}

////////////////////////////////
//~ rjf: @dmn_os_hooks Blocking Control Thread Operations (Implemented Per-OS)

internal DMN_CtrlCtx *
dmn_ctrl_begin(void)
{
  DMN_CtrlCtx *ctx = (DMN_CtrlCtx *)1;
  return ctx;
}

internal void
dmn_ctrl_exclusive_access_begin(void)
{
  MutexScope(mac_dmn_state->access_mutex)
  {
    mac_dmn_state->access_run_state = 1;
  }
}

internal void
dmn_ctrl_exclusive_access_end(void)
{
  MutexScope(mac_dmn_state->access_mutex)
  {
    mac_dmn_state->access_run_state = 0;
  }
}

internal U32
dmn_ctrl_launch(DMN_CtrlCtx *ctx, ProcessLaunchParams *params)
{
  Temp scratch = scratch_begin(0, 0);
  
  // setup target command line 
  U64    argc = params->cmd_line.node_count + 1;
  char **argv = push_array(scratch.arena, char *, argc);
  {
    U64 idx = 0;
    for EachNode(n, String8Node, params->cmd_line.first)
    {
      argv[idx] = (char *)str8_copy(scratch.arena, n->string).str;
      idx += 1;
    }
  }
  
  // setup target environment
  U64    envc = mac_state.default_env_count + params->env.node_count + 1;
  char **envp = push_array(scratch.arena, char *, envc);
  {
    // copy default environment
    MemoryCopyTyped(envp, mac_state.default_env, mac_state.default_env_count);
    
    // copy user environment
    U64 idx = mac_state.default_env_count;
    for EachNode(n, String8Node, params->env.first)
    {
      envp[idx] = (char *)str8_copy(scratch.arena, n->string).str;
      idx += 1;
    }
  }
  
  // create zero-terminated work directory path
  char *work_dir_path = (char *)str8_copy(scratch.arena, params->path).str;
  
  // spawn suspended child process
  pid_t pid = 0;

	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED | _POSIX_SPAWN_DISABLE_ASLR);

  // TODO(yuraiz): Investigate posix_spawnattr_setexceptionports_np and other apple expections

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addchdir(&actions, work_dir_path);

	int spawn_code = posix_spawnp(&pid, argv[0], &actions, &attr, argv, envp);

	posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);

  if(spawn_code == 0)
  {
    task_t task = 0;
    kern_return_t status_code = task_for_pid(mach_task_self(), pid, &task);
    
    if(status_code != 0)
    {
      fprintf(stderr, "failed to call task_for_pid on the child process: %s", mach_error_string(status_code));
      kill(pid, SIGTERM);
      return 0;
    }
    
    MAC_DMN_Process* process = mac_dmn_process_alloc(pid, MAC_DMN_ProcessState_Launch, 0, params->debug_subprocesses, 0);
    process->ctx = mac_dmn_process_ctx_alloc(process, 0);

    ptrace(PT_ATTACHEXC, pid, 0, 0);
  }

  scratch_end(scratch);
  return pid;
}

internal B32
dmn_ctrl_attach(DMN_CtrlCtx *ctx, U32 pid)
{
  task_t task = 0;
  kern_return_t status_code = task_for_pid(mach_task_self(), pid, &task);

  B32 is_attached = 0;
  if(status_code != 0)
  {
    if(task_suspend(task) == 0)
    {
      mac_dmn_process_alloc(pid, MAC_DMN_ProcessState_Attach, 0, 1, 0);
      is_attached = 1;
    }
  }
  return is_attached;
}

internal B32
dmn_ctrl_kill(DMN_CtrlCtx *ctx, DMN_Handle process_handle, U32 exit_code)
{
  B32 result = 0;
  mutex_take(mac_dmn_state->halter_mutex);
  MAC_DMN_Process *process = mac_dmn_process_from_handle(process_handle);
  if(process)
  {
    kern_return_t status_code = task_terminate(process->task) == 0;
    result = MAC_RETRY_ON_EINTR(kill(process->pid, SIGKILL)) >= 0;
    result = MAC_RETRY_ON_EINTR(ptrace(PT_KILL, process->pid, 0, 0)) >= 0;
  }
  mutex_drop(mac_dmn_state->halter_mutex);
  return result;
}

internal B32
dmn_ctrl_detach(DMN_CtrlCtx *ctx, DMN_Handle process_handle)
{
  // TODO(yuraiz)
  return 0;
}

internal DMN_EventList
dmn_ctrl_run(Arena *arena, DMN_CtrlCtx *ctx, DMN_RunCtrls *ctrls)
{
  Temp scratch = scratch_begin(&arena, 1);
  DMN_EventList events = {0};
  
  mutex_take(mac_dmn_state->halter_mutex);
  
  // wait for signals from the running threads
  if(mac_dmn_state->process_count > 0)
  {
    // write traps to memory
    MAC_DMN_ActiveTrap *active_trap_first = 0, *active_trap_last = 0;
    {
      HashTable *process_ht = hash_table_init(scratch.arena, mac_dmn_state->process_count);
      for EachNode(n, DMN_TrapChunkNode, ctrls->traps.first)
      {
        for EachIndex(n_idx, n->count)
        {
          // skip hardware breakpoints
          DMN_Trap *trap = n->v+n_idx;
          if(trap->flags) { continue; }
          
          HashTable *active_trap_ht = hash_table_search_u64_raw(process_ht, trap->process.u64[0]);
          if(active_trap_ht == 0)
          {
            active_trap_ht = hash_table_init(scratch.arena, ctrls->traps.trap_count);
            hash_table_push_u64_raw(scratch.arena, process_ht, trap->process.u64[0], active_trap_ht);
          }
          
          // TODO: ctrl sends down duplicate traps
          MAC_DMN_ActiveTrap *is_set = hash_table_search_u64_raw(active_trap_ht, trap->vaddr);
          if(is_set) { continue; }
          
          // TODO: ctrl sends down traps for exited process
          MAC_DMN_Process *process = mac_dmn_process_from_handle(trap->process);
          if(!process) { continue; }

          // trap instruction
          MAC_DMN_ActiveTrap *active_trap = mac_dmn_try_set_trap(scratch.arena, trap);
          if(active_trap != 0)
          {
            // add trap to the active list
            SLLQueuePush(active_trap_first, active_trap_last, active_trap);
            
            // add (address -> trap)
            hash_table_push_u64_raw(scratch.arena, active_trap_ht, trap->vaddr, active_trap);
          }
          else
          {
            // TODO(yuraiz): Somehow pass the failure to GUI
            printf("Failed to set trap %p\n", trap->vaddr);
          }
        }
      }
    }

    // enable single stepping
    if(!dmn_handle_match(ctrls->single_step_thread, dmn_handle_zero()))
    {
      MAC_DMN_Thread *single_step_thread = mac_dmn_thread_from_handle(ctrls->single_step_thread);
      if(single_step_thread)
      {
        mac_dmn_set_single_step_flag(single_step_thread, 1);
      }
      else
      {
        Assert(0 && "invalid single_step_thread handle");
      }
    }
    
    // schedule threads to run
    MAC_DMN_ThreadPtrList running_threads = {0};
    {
      for EachNode(process, MAC_DMN_Process, mac_dmn_state->first_process)
      {
        //- rjf: determine if this process is frozen
        B32 process_is_frozen = mac_dmn_state->is_halting;
        if(ctrls->run_entities_are_processes)
        {
          for EachIndex(idx, ctrls->run_entity_count)
          {
            if(dmn_handle_match(ctrls->run_entities[idx], mac_dmn_handle_from_process(process)))
            {
              process_is_frozen = 1;
              break;
            }
          }
        }
        
        for EachNode(thread, MAC_DMN_Thread, process->first_thread)
        {
          //- rjf: determine if this thread is frozen
          B32 is_frozen = 0;
          
          // rjf: not single-stepping? determine based on run controls freezing info
          if(dmn_handle_match(dmn_handle_zero(), ctrls->single_step_thread))
          {
            if(ctrls->run_entities_are_processes)
            {
              is_frozen = process_is_frozen;
            }
            else 
            {
              for EachIndex(idx, ctrls->run_entity_count)
              {
                if(dmn_handle_match(ctrls->run_entities[idx], mac_dmn_handle_from_thread(thread)))
                {
                  is_frozen = 1;
                  break;
                }
              }
            }
            if(ctrls->run_entities_are_unfrozen)
            {
              is_frozen ^= 1;
            }
          }
          // rjf: single-step? freeze if not the single-step thread.
          else
          {
            is_frozen = !dmn_handle_match(mac_dmn_handle_from_thread(thread), ctrls->single_step_thread);
          }
          
          struct thread_basic_info info;
          mach_msg_type_number_t info_cnt = THREAD_BASIC_INFO_COUNT;
          kern_return_t status_code = thread_info(thread->tid, THREAD_BASIC_INFO, (thread_info_t)&info, &info_cnt);
          //- yuraiz: when the task is killed, the port becomes invalid.
          if(status_code == 0)
          {
            B32 was_frozen = info.suspend_count > 0;
            
            // resume thread
            if(!is_frozen)
            {
              // AssertAlways(thread->state == MAC_DMN_ThreadState_Stopped);
              
              // write registers
              if(thread->is_reg_block_dirty)
              {
                thread->is_reg_block_dirty = !mac_dmn_thread_write_reg_block(thread);
              }
              
              // TODO(yuraiz): I'm not sure that actually needs implementation.
              // Don't we already pass the signals?
              //
              // pass signal to the child process
              // int sig_code = 0;
              // if(thread->pass_through_signal)
              // {
              //   thread->pass_through_signal = 0;
              //   if(!ctrls->ignore_previous_exception)
              //   {
              //     sig_code = (int)thread->pass_through_signo;
              //   }
              // }
              if(was_frozen)
              {
                thread_resume(thread->tid);
              }
            }
            else
            {
              if(!was_frozen)
              {
                thread_suspend(thread->tid);
              }
            }
          }
        }

        if(!process_is_frozen)
        {
          task_resume(process->task);
        }
      }
    }
    
    // hash running threads tids
    HashTable *running_threads_ht = hash_table_init(scratch.arena, running_threads.count * 2);
    for EachNode(n, MAC_DMN_ThreadPtrNode, running_threads.first)
    {
      hash_table_push_u64_raw(scratch.arena, running_threads_ht, n->v->tid, n);
    }
    
    B32                   is_halt_done    = 0;
    MAC_DMN_ThreadPtrList stopped_threads = {0};
    do
    {
      MAC_DMN_ExceptionResult result;
      {
        mutex_drop(mac_dmn_state->halter_mutex);
        result = mac_dmn_wait_for_exception(mac_dmn_state->exc_port);
        mutex_take(mac_dmn_state->halter_mutex);
      }

      for EachNode(process, MAC_DMN_Process, mac_dmn_state->first_process)
      {
        if(process->state == MAC_DMN_ProcessState_Normal)
        {
          //////////////////////////
          //-yuraiz monitor threads
          //
          thread_act_array_t threads = NULL;
          mach_msg_type_number_t threads_len = 0;
          task_threads(process->task, &threads, &threads_len);

          // generate exit thread events
          for EachNode(thread, MAC_DMN_Thread, process->first_thread)
          {
            B32 exists = 0;
            for EachIndex(i, threads_len)
            {
              if(thread->tid == threads[i])
              {
                exists = 1;
              }
            }
            if(!exists)
            {
              mac_dmn_event_exit_thread(arena, &events, thread->tid, 0);
            }
          }

          // generate new thread events
          for EachIndex(i, threads_len)
          {
            B32 exists = 0;
            for EachNode(thread, MAC_DMN_Thread, process->first_thread)
            {
              if(thread->tid == threads[i])
              {
                exists = 1;
              }
            }

            if(!exists)
            {
              mac_dmn_event_create_thread(arena, &events, process, threads[i]);
            }
          }

          vm_deallocate(mach_task_self(), (vm_address_t)threads, threads_len * sizeof(threads[0]));
        }
      }
      
      if(result.timed_out)
      {
        // NOTE(yuraiz): As I understand task_suspend is reliable enough
        if(mac_dmn_state->is_halting)
        {
          is_halt_done = 1;
          break;
        }

        continue;
      }

      // intercept initing processes
      {
        pid_t pid = 0;
        kern_return_t status_code = pid_for_task(result.task, &pid);
        MAC_DMN_Process *process = mac_dmn_process_from_pid(pid);
        
        if(process && process->state != MAC_DMN_ProcessState_Normal)
        {
          switch(process->state)
          {
            default: continue;
            case MAC_DMN_ProcessState_Null:
            case MAC_DMN_ProcessState_Normal:
            {
              InvalidPath;
            } break;
            case MAC_DMN_ProcessState_Attach:
            {
              if(result.exception == EXC_SOFTWARE &&
                 result.code == EXC_SOFT_SIGNAL &&
                 (result.subcode == SIGSTOP))
              {
                if(task_resume(result.task) == 0)
                {
                  process->state = MAC_DMN_ProcessState_Normal;
                  goto wait_for_signal;
                }
                else { Assert(0 && "failed to resume tracee"); }
              }
              else { Assert(0 && "unexpected signal"); }
            } break;
            case MAC_DMN_ProcessState_Launch:
            {
              if(result.exception == EXC_SOFTWARE &&
                 result.code == EXC_SOFT_SIGNAL &&
                 result.subcode == SIGSTOP)
              {
                printf("Handled the exception, process launched\n");

                MAC_DMN_CreateProcessFlags create_flags = process->debug_subprocesses ? MAC_DMN_CreateProcessFlag_DebugSubprocesses : 0;
                mac_dmn_process_release(process);
                process = mac_dmn_event_create_process(arena, &events, pid, 0, create_flags);

                // compute the initial dyld notifier address
                mac_dmn_process_update_dyld_notifier_addr(process);

                process->state = MAC_DMN_ProcessState_Normal;

                thread_act_array_t threads = NULL;
                mach_msg_type_number_t threads_len = 0;
                task_threads(process->task, &threads, &threads_len);
                for EachIndex(i, threads_len)
                {
                  mac_dmn_event_create_thread(arena, &events, process, threads[i]);
                }
                goto wait_for_signal;
              }
              else { Assert(0 && "unexpected signal"); }
            } break;
          }
          wait_for_signal:;
          continue;
        }
      }

  
      MAC_DMN_Thread *thread = mac_dmn_thread_from_pid(result.thread);

      // clear the exception if it's our probe and handle the probe
      if(result.exception == EXC_BREAKPOINT)
      {
        if(mac_dmn_event_probe_breakpoint(arena, &events, thread, result.subcode))
        {
          result.exception = 0;
        }
      }

      // // read thread registers
      // if(result.exception != 0)
      // {
      //   MAC_DMN_Thread *thread = mac_dmn_thread_from_pid(result.thread);
      //   if(thread != 0)
      //   {
      //     thread->is_reg_block_dirty = !mac_dmn_thread_read_reg_block(thread);
      //   }
      // }

      if(result.exception == EXC_BREAKPOINT)
      {
        MAC_DMN_Thread *thread = mac_dmn_thread_from_pid(result.thread);
        if(!mac_dmn_event_probe_breakpoint(arena, &events, thread, result.subcode))
        {
          // TODO(yuraiz): handle different types of breakpoints
          if(result.subcode == 0)
          {
            mac_dmn_event_single_step(arena, &events, result.thread);
          }
          else
          {
            mac_dmn_event_breakpoint(arena, &events, active_trap_first, result.thread);
          }
        }
        break;
      }
      
      if(result.exception == EXC_BAD_ACCESS)
      {
        // TODO(yuraiz): distinguish between different codes
        if(result.code == 257)
        {
          printf("bad access %d\n", result.subcode);
          MAC_DMN_Thread *thread = mac_dmn_thread_from_pid(result.thread);

          DMN_Event *e = dmn_event_list_push(arena, &events);
          e->kind                = DMN_EventKind_Exception;
          e->process             = mac_dmn_handle_from_process(thread->process);
          e->thread              = mac_dmn_handle_from_thread(thread);
          e->instruction_pointer = mac_dmn_thread_read_ip(thread);
          e->code                = SIGSEGV;
          e->exception_repeated  = 0;
          e->address             = result.subcode;
        }
        else
        {
          mac_dmn_event_exception(arena, &events, result.thread, SIGSEGV);
        }
        break;
      }
      if(result.exception == EXC_SOFTWARE && result.code == EXC_SOFT_SIGNAL)
      {
        mac_dmn_event_exception(arena, &events, result.thread, result.subcode);
        break;
      }
    // TODO(yuraiz): Fix running threads info
    } while(running_threads.count > 0 || mac_dmn_state->process_pending_creation > 0 || mac_dmn_state->threads_pending_creation > 0);
    
    // finalize halter state
    if(is_halt_done)
    {
      // push event
      mac_dmn_event_halt(arena, &events);
      
      // reset state
      mac_dmn_state->halter_tid     = 0;
      mac_dmn_state->halt_code      = 0;
      mac_dmn_state->halt_user_data = 0;
      mac_dmn_state->is_halting     = 0;
    }
    
    // restore original instruction bytes
    for EachNode(active_trap, MAC_DMN_ActiveTrap, active_trap_first)
    {
      // skip process that exited during the wait
      MAC_DMN_Process *process = mac_dmn_process_from_handle(active_trap->trap->process);
      if(!process) { continue; }
      
      if(!dmn_process_write(active_trap->trap->process, r1u64(active_trap->trap->vaddr, active_trap->trap->vaddr + active_trap->swap_bytes.size), active_trap->swap_bytes.str))
      {
        // Assert(0 && "failed to restore original instruction bytes");
      }
    }
  }

  // update register cache
  for EachNode(process, MAC_DMN_Process, mac_dmn_state->first_process)
  {
    for EachNode(thread, MAC_DMN_Thread, process->first_thread)
    {
      thread->is_reg_block_dirty = !mac_dmn_thread_read_reg_block(thread);
    }
  }
  
  if(events.count == 0 && mac_dmn_state->process_count == 0)
  {
    mac_dmn_push_event_not_attached(arena, &events);
  }
  
  mutex_drop(mac_dmn_state->halter_mutex);
  scratch_end(scratch);
  return events;
}

////////////////////////////////
//~ rjf: @dmn_os_hooks Halting (Implemented Per-OS)

internal void
dmn_halt(U64 code, U64 user_data)
{
  mutex_take(mac_dmn_state->halter_mutex);
  if(mac_dmn_state->process_count)
  {
    if(mac_dmn_state->process_count)
    {
      mac_dmn_state->halter_tid     = pthread_mach_thread_np(pthread_self());
      mac_dmn_state->halt_code      = code;
      mac_dmn_state->halt_user_data = user_data;
      mac_dmn_state->is_halting     = 1;
      for EachNode(process, MAC_DMN_Process, mac_dmn_state->first_process)
      {
        task_suspend(process->task);
      }
    }
  }
  mutex_drop(mac_dmn_state->halter_mutex);
}

////////////////////////////////
//~ rjf: @dmn_os_hooks Introspection Functions (Implemented Per-OS)

//- rjf: non-blocking-control-thread access barriers

internal B32
dmn_access_open(void)
{
  B32 result = 0;
  if(mac_dmn_ctrl_thread)
  {
    result = 1;
  }
  else
  {
    mutex_take(mac_dmn_state->access_mutex);
    result = !mac_dmn_state->access_run_state;
  }
  return result;
}

internal void
dmn_access_close(void)
{
  if(!mac_dmn_ctrl_thread)
  {
    mutex_drop(mac_dmn_state->access_mutex);
  }
}

//- rjf: processes

internal U64
dmn_process_memory_reserve(DMN_Handle process, U64 vaddr, U64 size)
{
  // TODO(yuraiz)
  return 0;
}

internal void
dmn_process_memory_commit(DMN_Handle process, U64 vaddr, U64 size)
{
  // TODO(yuraiz)
}

internal void
dmn_process_memory_decommit(DMN_Handle process, U64 vaddr, U64 size)
{
  // TODO(yuraiz)
}

internal void
dmn_process_memory_release(DMN_Handle process, U64 vaddr, U64 size)
{
  // TODO(yuraiz)
}

internal void
dmn_process_memory_protect(DMN_Handle process, U64 vaddr, U64 size, AccessFlags flags)
{
  // TODO(yuraiz)
}

internal U64
dmn_process_read(DMN_Handle process_handle, Rng1U64 range, void *dst)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(process_handle);
  return mac_dmn_process_read(process, range, dst);
}

internal B32
dmn_process_write(DMN_Handle process_handle, Rng1U64 range, void *src)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(process_handle);
  return mac_dmn_process_write(process, range, src);
}

//- rjf: threads

internal Arch
dmn_arch_from_thread(DMN_Handle thread_handle)
{
  // NOTE(yuraiz): Currently only macs on arm are supported.
  return Arch_arm64;
}

internal U64
dmn_stack_base_vaddr_from_thread(DMN_Handle thread_handle)
{
  U64 result = 0;
  MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(thread_handle);
  if(thread)
  {
    // TODO(yuraiz): maybe stackbottom is what's actually needed here?;
    result = thread->stackaddr;
  }
  return result;
}

internal B32
dmn_thread_get_module_tls_vaddr(DMN_Handle thread_handle, DMN_Handle module_handle, U64 *vaddr_out)
{
  // TODO(yuraiz)
  B32 result = 0;
  MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(thread_handle);
  if(thread)
  {
    // TODO(yuraiz): I guess the thread handle changes until the modules are finally loaded.

    struct thread_identifier_info identifier_info;
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    kern_return_t kr = thread_info(thread->tid,
                                  THREAD_IDENTIFIER_INFO,
                                  (thread_info_t)&identifier_info,
                                  &count);
  
    thread->thread_local_base = identifier_info.thread_handle;
    
    *vaddr_out = thread->thread_local_base;
    result = 1;
  }
  return result;
}

internal U64
dmn_tls_root_vaddr_from_thread(DMN_Handle thread_handle)
{
  // TODO(yuraiz)
  U64 result = 0;
  MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(thread_handle);
  if(thread)
  {
    // TODO(yuraiz): I guess the thread handle changes until the modules are finally loaded.

    struct thread_identifier_info identifier_info;
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    kern_return_t kr = thread_info(thread->tid,
                                  THREAD_IDENTIFIER_INFO,
                                  (thread_info_t)&identifier_info,
                                  &count);
  
    thread->thread_local_base = identifier_info.thread_handle;
    
    result = thread->thread_local_base;
  }
  return result;
}

internal B32
dmn_thread_read_reg_block(DMN_Handle thread_handle, void *reg_block)
{
  B32 result = 0;
  MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(thread_handle);
  if(thread)
  {
    ARCH_Info *arch_info = arch_info_from_arch(thread->process->ctx->arch);
    U64 reg_block_size = arch_info->reg_block_size;
    MemoryCopy(reg_block, thread->reg_block, reg_block_size);
    result = 1;
  }
  return result;
}

internal B32
dmn_thread_write_reg_block(DMN_Handle thread_handle, void *reg_block)
{
  B32 result = 0;
  MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(thread_handle);
  if(thread)
  {
    ARCH_Info *arch_info = arch_info_from_arch(thread->process->ctx->arch);
    U64 reg_block_size = arch_info->reg_block_size;
    MemoryCopy(thread->reg_block, reg_block, reg_block_size);
    thread->is_reg_block_dirty = 1;
    result = 1;
  }
  return result;
}

//- rjf: system process listing

internal void
dmn_process_iter_begin(DMN_ProcessIter *iter)
{
  NSArray* apps = [[NSWorkspace sharedWorkspace] runningApplications];
  iter->v[0] = (U64)apps;
  iter->v[1] = 0;
}

internal B32
dmn_process_iter_next(Arena *arena, DMN_ProcessIter *iter, DMN_ProcessInfo *info_out)
{
  NSArray* apps = (NSArray*)iter->v[0];
  NSRunningApplication* app = apps[iter->v[1]];
  iter->v[1] += 1;

  NSString* app_name = app.localizedName;

  info_out->name = str8_copy(arena, str8_cstring(app_name.UTF8String));
  info_out->pid = app.processIdentifier;

  [app_name release];

  return iter->v[1] != apps.count;
}

internal void
dmn_process_iter_end(DMN_ProcessIter *iter)
{
  NSArray* apps = (NSArray*)iter->v[0];
  [apps release];
}
