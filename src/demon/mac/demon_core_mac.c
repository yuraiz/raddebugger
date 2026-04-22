// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Includes

#include <mach/mach_vm.h>

#include "demon_os_mac.c"

////////////////////////////////
//~ Helpers

internal DMN_MAC_ImageArray
dmn_mac_read_dyld_images(Arena *arena, DMN_MAC_Process *process)
{
  DMN_MAC_ImageArray result = {0};

  struct task_dyld_info info;
  mach_msg_type_number_t info_cnt = TASK_DYLD_INFO_COUNT;
  kern_return_t status_code = task_info(process->task, TASK_DYLD_INFO, (task_info_t)&info, &info_cnt);
  if(status_code != 0)
  {
    if(status_code != MACH_SEND_INVALID_DEST)
    {
      fprintf(stderr, "Failed to read task dyld info: %s\n", mach_error_string(status_code));
    }
    return result;
  }
  Assert(info.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_64);

  struct dyld_all_image_infos all_image_infos;
  mach_vm_size_t readCount = 0;
  status_code = mach_vm_read_overwrite(process->task, info.all_image_info_addr, sizeof(all_image_infos),  (mach_vm_address_t)&all_image_infos, &readCount);
  if(status_code != 0)
  {
    fprintf(stderr, "Failed to read task dyld all image info info\n");
  }

  struct dyld_image_info *image_info_array = push_array(arena, struct dyld_image_info, all_image_infos.infoArrayCount);
  mach_vm_size_t array_size = sizeof(struct dyld_image_info) * all_image_infos.infoArrayCount;
  readCount = 0;
  status_code = mach_vm_read_overwrite(process->task, (mach_vm_address_t)all_image_infos.infoArray, array_size, (mach_vm_address_t)image_info_array, &readCount);
  if(status_code != 0)
  {
    fprintf(stderr, "Failed to read task dyld image info array: %s\n", mach_error_string(status_code));
  }

  result.count = all_image_infos.infoArrayCount;
  result.items = image_info_array;

  return result;
}

internal B32
dmn_mac_write_to_protected(
    const task_t task,
    const mach_vm_address_t address,
    const void* data,
    const size_t size,
    const bool revert_back
) {
    // NOTE(yuraiz): on macOS memory writable XOR executable.
    // so we need to change the protection before writing to it and revert back after.

    mach_port_t object_name = MACH_PORT_NULL;
    mach_msg_type_number_t region_info_size = VM_REGION_BASIC_INFO_COUNT_64;

    vm_region_basic_info_64_t region_info = alloca(sizeof(*region_info));

    mach_vm_address_t region_address = address;
    mach_vm_size_t region_size = (mach_vm_size_t)size;
    mach_vm_region(
        task,
        &region_address,
        &region_size,
        VM_REGION_BASIC_INFO_64,
        (vm_region_info_t)region_info,
        &region_info_size,
        &object_name
    );

    const vm_prot_t old_protection = region_info->protection;

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

        mach_vm_protect(
            task,
            region_address,
            region_size,
            false,
            new_protection | VM_PROT_COPY
        );
    }

    kern_return_t status_code = mach_vm_write(
        task, address, (vm_offset_t)data, (mach_msg_type_number_t)size
    );

    // Re-protect the region back to the way it was
    if ((revert_back || executable_protection_modified) &&
        needs_to_change_protection) {
        mach_vm_protect(
            task, region_address, region_size, false, old_protection
        );

        if (executable_protection_modified) {
            task_resume(task);
        }
    }

    return status_code == 0;
}

internal kern_return_t
dmn_mac_vm_region_recurse_deepest(
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
dmn_mac_compute_stack_range(
  mach_port_t task,
  mach_vm_address_t* address,
  mach_vm_size_t* size)
{ 
  natural_t depth;
  vm_prot_t protection;
  unsigned int user_tag;
  
  // TODO(yuraiz): Check if the stack can consist from multiple segments.
  kern_return_t status_code =
  dmn_mac_vm_region_recurse_deepest(task, address, size, &depth, &protection, &user_tag);
  
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

internal B32
dmn_mac_set_single_step_flag(DMN_MAC_Thread *thread, B32 is_on)
{
  B32 is_flag_set = 0;
  switch(thread->process->ctx->arch)
  {
    case Arch_Null: {} break;
    case Arch_x64:
    {
      REGS_RegBlockX64 *reg_block = thread->reg_block;
      if(is_on) { reg_block->rflags.u64 |= X64_RFlag_Trap;  }
      else      { reg_block->rflags.u64 &= ~X64_RFlag_Trap; }
      thread->is_reg_block_dirty = 1;
      is_flag_set = 1;
    } break;
    case Arch_arm64:
    {
      // Set SS (Single Stepping) bit
      if(is_on) { thread->reg_mdscr_el1 |= 0x1;    }
      else      { thread->reg_mdscr_el1 |= ~(0x1); }
      thread->is_reg_block_dirty = 1;
      is_flag_set = 1;
    } break;
    case Arch_x86:
    case Arch_arm32: { NotImplemented; }break;
    default: { InvalidPath; } break;
  }
  Assert(is_flag_set);
  return is_flag_set;
}

internal U64
dmn_mac_thread_read_ip(DMN_MAC_Thread *thread)
{
  return regs_rip_from_arch_block(thread->process->ctx->arch, thread->reg_block);
}

internal U64
dmn_mac_thread_read_sp(DMN_MAC_Thread *thread)
{
  return regs_rsp_from_arch_block(thread->process->ctx->arch, thread->reg_block);
}

internal void
dmn_mac_thread_write_ip(DMN_MAC_Thread *thread, U64 ip)
{
  regs_arch_block_write_rip(thread->process->ctx->arch, thread->reg_block, ip);
  thread->is_reg_block_dirty = 1;
}

internal void
dmn_mac_thread_write_sp(DMN_MAC_Thread *thread, U64 sp)
{
  regs_arch_block_write_rsp(thread->process->ctx->arch, thread->reg_block, sp);
  thread->is_reg_block_dirty = 1;
}

internal B32
dmn_mac_thread_read_reg_block(DMN_MAC_Thread *thread)
{
  switch(thread->process->ctx->arch)
  {
    case Arch_Null: {} break;
    case Arch_arm64:
    {
      REGS_RegBlockARM64 *reg_block = thread->reg_block;
      
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
      reg_block->fp.u64 = thread_state.__fp;
      reg_block->lr.u64 = thread_state.__lr;
      reg_block->sp.u64 = thread_state.__sp;
      reg_block->pc.u64 = thread_state.__pc;

      MemoryCopy(&reg_block->v0, neon_state.__v, sizeof(neon_state.__v));

      thread->reg_mdscr_el1 = debug_state.__mdscr_el1;
	
      return true;
    } break;
    case Arch_x64:
    case Arch_x86:
    case Arch_arm32: { NotImplemented; }break;
    default: { InvalidPath; } break;
  }
  return false;
}

internal B32
dmn_mac_thread_write_reg_block(DMN_MAC_Thread *thread)
{
  switch(thread->process->ctx->arch)
  {
    case Arch_Null: {} break;
    case Arch_arm64:
    {
      REGS_RegBlockARM64 *reg_block = thread->reg_block;
      
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
      thread_state.__fp = reg_block->fp.u64;
      thread_state.__lr = reg_block->lr.u64;
      thread_state.__sp = reg_block->sp.u64;
      thread_state.__pc = reg_block->pc.u64;
      
      MemoryCopy(neon_state.__v, &reg_block->v0, sizeof(neon_state.__v));

      debug_state.__mdscr_el1 = thread->reg_mdscr_el1;
      
      count = ARM_THREAD_STATE64_COUNT;
      thread_set_state(thread->tid, ARM_THREAD_STATE64, (thread_state_t)&thread_state, count);
      count = ARM_NEON_STATE64_COUNT;
      thread_set_state(thread->tid, ARM_NEON_STATE64, (thread_state_t)&neon_state, count);
      count = ARM_DEBUG_STATE64_COUNT;
      thread_set_state(thread->tid, ARM_DEBUG_STATE64, (thread_state_t)&debug_state, count);

      return true;
    } break;
    case Arch_x64:
    case Arch_x86:
    case Arch_arm32: { NotImplemented; }break;
    default: { InvalidPath; } break;
  }
  return false;
}

internal U64
dmn_mac_process_read(DMN_MAC_Process* process, Rng1U64 range, void *dst)
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
dmn_mac_process_write(DMN_MAC_Process* process, Rng1U64 range, void *src)
{
  U64 result = 0;
  if(process)
  {
    U64 to_write = dim_1u64(range);
    result = dmn_mac_write_to_protected(process->task, range.min, src, to_write, true);
  }
  return 0;
}

internal
STAP_MEMORY_READ(dmn_mac_stap_memory_read)
{
  DMN_MAC_Process *process = raw_ctx;
  U64 bytes_read = dmn_mac_process_read(process, r1u64(addr, addr + read_size), buffer);
  return bytes_read == read_size;
}

internal
MACHINE_OP_MEM_READ(dmn_mac_machine_op_mem_read)
{
  U64 read_size = dmn_mac_process_read((DMN_MAC_Process *)ud, r1u64(addr, addr + buffer_size), buffer);
  return read_size == buffer_size ? MachineOpResult_Ok : MachineOpResult_Fail;
}

internal B32
dmn_mac_mach_read_op_mem(U64 address, U64 size, void* dst, U64 src)
{
  U64 read_size = 0;
  mach_vm_read_overwrite(src, address, size, (mach_vm_address_t)dst, &read_size);
  return read_size == size;
}

internal DMN_MAC_Entity *
dmn_mac_entity_alloc(DMN_MAC_EntityKind kind)
{
  DMN_MAC_Entity *entity = dmn_mac_state->free_entity;
  if(entity != 0)
  {
    SLLStackPop(dmn_mac_state->free_entity);
  }
  else
  {
    entity = push_array(dmn_mac_state->entities_arena, DMN_MAC_Entity, 1);
    dmn_mac_state->entities_count += 1;
  }
  U32 gen = entity->gen;
  entity->gen += 1;
  entity->kind = kind;
  return entity;
}

internal DMN_MAC_Process *
dmn_mac_process_alloc(pid_t pid, DMN_MAC_ProcessState state, DMN_MAC_Process *parent_process, B32 debug_subprocesses, B32 is_cow)
{
  Temp scratch = scratch_begin(0, 0);
  
  DMN_MAC_Process *process = &dmn_mac_entity_alloc(DMN_MAC_EntityKind_Process)->process;
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

  dmn_mac_subscribe_to_exceptions(process->task, dmn_mac_state->exc_port);
  
  // update pending process tracker
  if(state != DMN_MAC_ProcessState_Normal)
  {
    dmn_mac_state->process_pending_creation += 1;
  }
  
  // add process to the list
  DLLPushBack(dmn_mac_state->first_process, dmn_mac_state->last_process, process);
  dmn_mac_state->process_count += 1;
  
  // push pid -> DMN_MAC_Process mapping
  hash_table_push_u64_raw(dmn_mac_state->arena, dmn_mac_state->pid_ht, pid, process);
  
  scratch_end(scratch);
  return process;
}

internal DMN_MAC_Module *
dmn_mac_module_alloc(DMN_MAC_Process *process, U64 load_address, U64 name_vaddr)
{
  Temp scratch = scratch_begin(0, 0);
  DMN_MAC_Module *module = &dmn_mac_entity_alloc(DMN_MAC_EntityKind_Module)->module;

  // TODO(yuraz): I guess we can read more useful info from here
  MACH_Bin info = mach_extract_file_info(scratch.arena, load_address, dmn_mac_mach_read_op_mem, process->task);
  
  DLLPushBack(process->ctx->first_module, process->ctx->last_module, module);
  process->ctx->module_count += 1;

  // NOTE(yuraiz): That address expected to have the offset in some places,
  // but in other's it should be just load_address.
  // That offset ctrl_thread__module_open magic detection, but is required for the correct symbol mapping.
  module->base_vaddr = load_address - mach_compute_image_offset(info); 
  module->name_vaddr = name_vaddr;
  module->size       = mach_compute_image_size(info);
  
  scratch_end(scratch);
  return module;
}

internal DMN_MAC_ProcessCtx *
dmn_mac_process_ctx_alloc(DMN_MAC_Process *process, B32 is_rebased)
{
  // DMN_MAC_ProcessCtx *ctx = &dmn_mac_entity_alloc(DMN_MAC_EntityKind_ProcessCtx)->process_ctx;
  
//   ELF_Hdr64     exe_ehdr     = dmn_mac_ehdr_from_pid(process->pid);
//   DMN_MAC_Auxv  auxv         = dmn_mac_auxv_from_pid(process->pid, exe_ehdr.e_ident[ELF_Identifier_Class]);
//   Arch          arch         = arch_from_elf_machine(exe_ehdr.e_machine);
//   U64           rdebug_vaddr = dmn_mac_rdebug_vaddr_from_memory(process->fd, auxv.base, is_rebased);
//   U64           base_vaddr   = (auxv.phdr & ~(auxv.pagesz-1));
//   U64           rebase       = exe_ehdr.e_type == ELF_Type_Dyn ? base_vaddr : 0;
//   Rng1U64       image_vrange = dmn_mac_compute_image_vrange(process->fd, exe_ehdr.e_ident[ELF_Identifier_Class], rebase, auxv.phdr, auxv.phent, auxv.phnum);
//   Arena        *ctx_arena    = arena_alloc();
  
//   ELF_Class dl_class;
//   {
//     ELF_Hdr64 ehdr = {0};
//     if(elf_read_ehdr(dmn_mac_machine_op_mem_read, &process->fd, auxv.base, &ehdr) != MachineOpResult_Ok) { Assert(0 && "failed to read interp's header"); }
//     dl_class = ehdr.e_ident[ELF_Identifier_Class];
//   }
  
//   // gather probes
//   DMN_MAC_Probe **known_probes = push_array(ctx_arena, DMN_MAC_Probe *, DMN_MAC_ProbeType_Count);
//   {
//     Temp scratch = scratch_begin(0, 0);
    
//     String8 dl_path = dmn_mac_dl_path_from_pid(scratch.arena, process->pid, auxv.base);
//     int dl_fd = OS_LNX_RETRY_ON_EINTR(open((char *)dl_path.str, O_RDONLY));
    
//     DMN_MAC_ProbeList probes = {0};
//     if(dl_fd >= 0)
//     {
//       probes = dmn_mac_read_probes(ctx_arena, dl_fd, 0, auxv.base);
//       OS_LNX_RETRY_ON_EINTR(close(dl_fd));
//     }
    
//     for EachNode(n, DMN_MAC_ProbeNode, probes.first)
//     {
//       DMN_MAC_Probe *p = &n->v;
//       if(str8_match(p->provider, str8_lit("rtld"), 0))
//       {
// #define X(_N,_A,_S) if(str8_match(p->name, str8_lit(_S), 0)) { AssertAlways(p->args.count == _A); known_probes[DMN_MAC_ProbeType_##_N] = p; continue ; }
//         DMN_MAC_Probe_XList
// #undef X
//       }
//     }
    
//     scratch_end(scratch);
//   }
  
  DMN_MAC_ProcessCtx *ctx = &dmn_mac_entity_alloc(DMN_MAC_EntityKind_ProcessCtx)->process_ctx;
  ctx->arena             = arena_alloc();
  ctx->arch              = Arch_arm64;
  // ctx->rdebug_vaddr      = rdebug_vaddr;
  // ctx->dl_class          = dl_class;
  ctx->loaded_modules_ht = hash_table_init(ctx->arena, 0x1000);
  // ctx->probes            = known_probes;
  
  // TODO(yuraiz)
  // // create main module
  // DMN_MAC_Module *main_module = dmn_mac_module_alloc(ctx, process->fd, base_vaddr, auxv.execfn, 1, 1);
  
  // // glibc has a shortcut mapping for the main module
  // hash_table_push_u64_raw(ctx->arena, ctx->loaded_modules_ht, 0, main_module);
  
  return ctx;
}

internal DMN_MAC_Thread *
dmn_mac_thread_alloc(DMN_MAC_Process *process, DMN_MAC_ThreadState thread_state, thread_act_t tid)
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
    U64 reg_block_size = regs_block_size_from_arch(process->ctx->arch);
    reg_block = push_array(process->ctx->arena, U8, reg_block_size);
  }
  
  DMN_MAC_Thread *thread = &dmn_mac_entity_alloc(DMN_MAC_EntityKind_Thread)->thread;
  thread->tid       = tid;
  thread->state     = thread_state;
  thread->process   = process;
  thread->reg_block = reg_block;

  // NOTE(yuraiz): It's safe to read the registers even if the thread isn't suspended.
  thread->is_reg_block_dirty = !dmn_mac_thread_read_reg_block(thread);
  
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
  U64 stack_pointer = dmn_mac_thread_read_sp(thread);
  mach_vm_size_t stack_size = 0;
  dmn_mac_compute_stack_range(thread->process->task, &stack_pointer, &stack_size);

  thread->stackaddr = stack_pointer;
  thread->stackbottom = stack_pointer - stack_size;

  // add thread to the list
  DLLPushBack(process->first_thread, process->last_thread, thread);
  process->thread_count += 1;
  
  // push tid -> thread mapping
  hash_table_push_u64_raw(dmn_mac_state->arena, dmn_mac_state->tid_ht, thread->tid, thread);
  
  // update global thread counter
  if(thread_state == DMN_MAC_ThreadState_PendingCreation)
  {
    dmn_mac_state->threads_pending_creation += 1;
  }
  
  return thread;
}

internal void
dmn_mac_entity_release(DMN_MAC_Entity *entity)
{
  U32 gen = entity->gen + 1;
  MemoryZeroStruct(entity);
  entity->gen = gen;
  SLLStackPush(dmn_mac_state->free_entity, entity);
}

internal void
dmn_mac_process_release(DMN_MAC_Process *process)
{
  // update global state
  AssertAlways(dmn_mac_state->process_count > 0);
  DLLRemove(dmn_mac_state->first_process, dmn_mac_state->last_process, process);
  dmn_mac_state->process_count -= 1;
  
  // update pending process tracker
  if(process->state != DMN_MAC_ProcessState_Normal)
  {
    Assert(dmn_mac_state->process_pending_creation > 0);
    dmn_mac_state->process_pending_creation -= 1;
  }
  
  // remove pid mapping
  hash_table_purge_u64(dmn_mac_state->pid_ht, process->pid);
  
  // release the context
  if(process->ctx)
  {
    // TODO(yuraiz)
    // dmn_mac_process_ctx_release(process->ctx);
  }
  
  // release process entity
  dmn_mac_entity_release((DMN_MAC_Entity *)process);
}

internal void
dmn_mac_process_ctx_release(DMN_MAC_ProcessCtx *ctx)
{
  Assert(ctx->ref_count > 0);
  ctx->ref_count -= 1;
  
  if(ctx->ref_count == 0)
  {
    arena_release(ctx->arena);
    dmn_mac_entity_release((DMN_MAC_Entity *)ctx);
  }
}

internal void
dmn_mac_thread_release(DMN_MAC_Thread *thread)
{
  DMN_MAC_Process *process = thread->process;
  
  // purge tid mapping
  hash_table_purge_u64(dmn_mac_state->tid_ht, thread->tid);
  
  // update global thread counter
  if(thread->state == DMN_MAC_ThreadState_PendingCreation)
  {
    AssertAlways(dmn_mac_state->threads_pending_creation > 0);
    dmn_mac_state->threads_pending_creation -= 1;
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
  
  dmn_mac_entity_release((DMN_MAC_Entity *)thread);
}

internal void
dmn_mac_module_release(DMN_MAC_ProcessCtx *ctx, DMN_MAC_Module *module)
{
  // remove module from the list
  Assert(ctx->module_count > 0);
  DLLRemove(ctx->first_module, ctx->last_module, module);
  ctx->module_count -= 1;
  
  // purge base addr -> module mapping
  hash_table_purge_u64(ctx->loaded_modules_ht, module->base_vaddr);
  
  dmn_mac_entity_release((DMN_MAC_Entity *)module);
}

internal DMN_Handle
dmn_mac_handle_from_entity(DMN_MAC_Entity *entity)
{
  DMN_Handle handle = {0};
  U64 index = IntFromPtr(entity);
  handle.u64[0] = index;
  // handle.u32[0] = index;
  // handle.u32[1] = entity->gen;
  return handle;
}

internal DMN_Handle
dmn_mac_handle_from_process(DMN_MAC_Process *process)
{
  return dmn_mac_handle_from_entity((DMN_MAC_Entity *)process);
}

internal DMN_Handle
dmn_mac_handle_from_process_ctx(DMN_MAC_ProcessCtx *process_ctx)
{
  return dmn_mac_handle_from_entity((DMN_MAC_Entity *)process_ctx);
}

internal DMN_Handle
dmn_mac_handle_from_thread(DMN_MAC_Thread *thread)
{
  return dmn_mac_handle_from_entity((DMN_MAC_Entity *)thread);
}

internal DMN_Handle
dmn_mac_handle_from_module(DMN_MAC_Module *module)
{
  return dmn_mac_handle_from_entity((DMN_MAC_Entity *)module);
}

internal DMN_MAC_Entity *
dmn_mac_entity_from_handle(DMN_Handle handle, DMN_MAC_EntityKind expected_kind)
{
  // TODO(yuraiz): Figure out what "gen" was for
  DMN_MAC_Entity *result = (DMN_MAC_Entity *)handle.u64[0];
  return result;
}

internal DMN_MAC_Process *
dmn_mac_process_from_handle(DMN_Handle process_handle)
{
  return (DMN_MAC_Process *)dmn_mac_entity_from_handle(process_handle, DMN_MAC_EntityKind_Process);
}

internal DMN_MAC_ProcessCtx *
dmn_mac_process_ctx_from_handle(DMN_Handle process_ctx_handle)
{
  return (DMN_MAC_ProcessCtx *)dmn_mac_entity_from_handle(process_ctx_handle, DMN_MAC_EntityKind_ProcessCtx);
}

internal DMN_MAC_Thread *
dmn_mac_thread_from_handle(DMN_Handle thread_handle)
{
  return (DMN_MAC_Thread *)dmn_mac_entity_from_handle(thread_handle, DMN_MAC_EntityKind_Thread);
}

internal DMN_MAC_Module *
dmn_mac_module_from_handle(DMN_Handle module_handle)
{
  return (DMN_MAC_Module *)dmn_mac_entity_from_handle(module_handle, DMN_MAC_EntityKind_Module);
}

internal DMN_MAC_Thread *
dmn_mac_thread_from_pid(pid_t tid)
{
  return hash_table_search_u64_raw(dmn_mac_state->tid_ht, tid);
}

internal DMN_MAC_Process *
dmn_mac_process_from_pid(pid_t pid)
{
  return hash_table_search_u64_raw(dmn_mac_state->pid_ht, pid);
}

// event helpers

internal void
dmn_mac_push_event_create_process(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process)
{
  Temp scratch = scratch_begin(&arena, 1);
  // push create process event
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind      = DMN_EventKind_CreateProcess;
  e->process   = dmn_mac_handle_from_process(process);
  e->arch      = process->ctx->arch;
  e->code      = process->pid;
  e->tls_model = DMN_TlsModel_Gnu; // TODO: use dynamic linker path to figure out correct enum here

  scratch_end(scratch);
}

internal void
dmn_mac_push_event_exit_process(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_ExitProcess;
  e->process = dmn_mac_handle_from_process(process);
  e->code    = process->main_thread_exit_code;
}

internal void
dmn_mac_push_event_create_thread(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_CreateThread;
  e->process             = dmn_mac_handle_from_process(thread->process);
  e->thread              = dmn_mac_handle_from_thread(thread);
  e->arch                = thread->process->ctx->arch;
  e->code                = thread->tid;
  e->instruction_pointer = dmn_mac_thread_read_ip(thread);

  struct thread_extended_info info;
  mach_msg_type_number_t info_cnt = THREAD_EXTENDED_INFO_COUNT;
  kern_return_t status_code = thread_info(thread->tid, THREAD_EXTENDED_INFO, (thread_info_t)&info, &info_cnt);
  if(info.pth_name[0] != 0)
  {
    e = dmn_event_list_push(arena, events);
    e->kind    = DMN_EventKind_SetThreadName;
    e->process = dmn_mac_handle_from_process(thread->process);
    e->thread  = dmn_mac_handle_from_thread(thread);

    e->string  = str8_copy(arena, str8_cstring(info.pth_name));
    e->code    = thread->tid;
  }
}

internal void
dmn_mac_push_event_exit_thread(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread, U64 exit_code)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_ExitThread;
  e->process = dmn_mac_handle_from_process(thread->process);
  e->thread  = dmn_mac_handle_from_thread(thread);
  e->code    = exit_code;
}

internal void
dmn_mac_push_event_load_module(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, DMN_MAC_Module *module)
{
  Temp scratch = scratch_begin(&arena, 1);

  char* path_buffer = push_array(scratch.arena, char, PATH_MAX);
  dmn_mac_mach_read_op_mem(module->name_vaddr, PATH_MAX, path_buffer, process->task);
  String8 path = str8_cstring(path_buffer);

  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind            = DMN_EventKind_LoadModule;
  e->process         = dmn_mac_handle_from_process(process);
  e->module          = dmn_mac_handle_from_module(module);
  e->arch            = process->ctx->arch;
  e->address         = module->base_vaddr;
  e->string          = push_str8_copy(arena, path);
  e->size            = module->size;

  scratch_end(scratch);
}

internal void
dmn_mac_push_event_unload_module(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, DMN_MAC_Module *module)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_UnloadModule;
  e->process = dmn_mac_handle_from_process(process);
  e->module  = dmn_mac_handle_from_module(module);
}

internal void
dmn_mac_push_event_handshake_complete(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind    = DMN_EventKind_HandshakeComplete;
  e->process = dmn_mac_handle_from_process(process);
  e->thread  = dmn_mac_handle_from_thread(process->first_thread);
  e->arch    = process->ctx->arch;
}

internal void
dmn_mac_push_event_breakpoint(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread, U64 address)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_Breakpoint;
  e->process             = dmn_mac_handle_from_process(thread->process);
  e->thread              = dmn_mac_handle_from_thread(thread);
  e->instruction_pointer = address;
}

internal void
dmn_mac_push_event_single_step(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_SingleStep;
  e->process             = dmn_mac_handle_from_process(thread->process);
  e->thread              = dmn_mac_handle_from_thread(thread);
  e->instruction_pointer = dmn_mac_thread_read_ip(thread);
}

internal void
dmn_mac_push_event_exception(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread, U64 signo)
{
  local_persist B8 is_repeatable[] =
  {
    0, // null
    1, // 1 SIGHUP
    1, // 2 SIGINT
    1, // 3 SIGQUIT
    1, // 4 SIGTRAP
    1, // 5 SIGABRT
    1, // 6 SIGIOT
    1, // 7 SIGBUS
    1, // 8 SIGFPE
    1, // 9 SIGKILL
    1, // 10 SIGUSR1
    1, // 11 SIGSEGV
    1, // 12 SIGUSR2
    1, // 13 SIGPIPE
    1, // 14 SIGALRM
    1, // 15 SIGTERM
    1, // 16 SIGTKFLT
    0, // 17 SIGCHLD
    0, // 18 SIGCONT
    1, // 19 SIGSTOP
    1, // 20 SIGTSP
    1, // 21 SIGTTIN
    1, // 22 SIGTTOU
    0, // 23 SIGURG
    1, // 24 SIGXCPU
    1, // 25 SIGXFSZ
    1, // 26 SIGVTALRM
    1, // 27 SIGPROF
    0, // 28 SIGWINCH
    1, // 29 SIGIO
    1, // 30 SIGPWR
    1, // 31 SIGSYS
    1, // 32 SIGUNUSED
  };
  
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind                = DMN_EventKind_Exception;
  e->process             = dmn_mac_handle_from_process(thread->process);
  e->thread              = dmn_mac_handle_from_thread(thread);
  e->instruction_pointer = dmn_mac_thread_read_ip(thread);
  e->signo               = signo;
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
dmn_mac_push_event_halt(Arena *arena, DMN_EventList *events)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_Halt;
}

internal void
dmn_mac_push_event_not_attached(Arena *arena, DMN_EventList *events)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind       = DMN_EventKind_Error;
  e->error_kind = DMN_ErrorKind_NotAttached;
}

internal DMN_MAC_Thread *
dmn_mac_event_create_thread(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, thread_act_t tid)
{
  DMN_MAC_Thread *thread = dmn_mac_thread_alloc(process, DMN_MAC_ThreadState_Stopped, tid);
  dmn_mac_push_event_create_thread(arena, events, thread);
  return thread;
}

internal void
dmn_mac_event_exit_thread(Arena *arena, DMN_EventList *events, pid_t tid, U64 exit_code)
{
  DMN_MAC_Thread  *thread  = dmn_mac_thread_from_pid(tid);
  DMN_MAC_Process *process = thread->process;
  
  // store main thread's exit code
  if(thread->tid == thread->process->pid)
  {
    thread->process->main_thread_exit_code = exit_code;
  }
  
  // push exit event
  dmn_mac_push_event_exit_thread(arena, events, thread, exit_code);
  
  // release entity
  dmn_mac_thread_release(thread);
  
  // auto exit process on last thread
  if(process->thread_count == 0)
  {
    dmn_mac_event_exit_process(arena, events, process->pid);
  }
}

internal DMN_MAC_Process *
dmn_mac_event_create_process(Arena *arena, DMN_EventList *events, pid_t pid, DMN_MAC_Process *parent_process, DMN_MAC_CreateProcessFlags flags)
{
  DMN_MAC_Process *process = dmn_mac_process_alloc(pid, DMN_MAC_ProcessState_Normal, parent_process, !!(flags & DMN_MAC_CreateProcessFlag_DebugSubprocesses), !!(flags & DMN_MAC_CreateProcessFlag_Cow));
  
  if(flags & DMN_MAC_CreateProcessFlag_ClonedMemory)
  {
    process->ctx = parent_process->ctx;
  }
  else
  {
    // TODO(yuraiz)
    process->ctx = dmn_mac_process_ctx_alloc(process, !!(flags & DMN_MAC_CreateProcessFlag_Rebased));
  }
  process->ctx->ref_count += 1;
  
  // install probes in a process that does not have a cloned memory
  if(!(flags & DMN_MAC_CreateProcessFlag_ClonedMemory))
  {
    // TODO(yuraiz)
    // dmn_mac_process_trap_probes(process);
  }
  
  // push events
  dmn_mac_push_event_create_process(arena, events, process);
  for EachNode(thread, DMN_MAC_Thread, process->first_thread)
  {
    dmn_mac_push_event_create_thread(arena, events, thread);
  }

  // TODO(yuraiz): Pushing a single module, in a different place
 
  for EachNode(module, DMN_MAC_Module, process->ctx->first_module)
  {
    dmn_mac_push_event_load_module(arena, events, process, module);
  }
  dmn_mac_push_event_handshake_complete(arena, events, process);
  
  return process;
}

internal void
dmn_mac_event_exit_process(Arena *arena, DMN_EventList *events, pid_t pid)
{
  DMN_MAC_Process *process = dmn_mac_process_from_pid(pid);
  AssertAlways(process->thread_count == 0);
  
  // push module events
  for EachNode(module, DMN_MAC_Module, process->ctx->first_module)
  {
    dmn_mac_push_event_unload_module(arena, events, process, module);
  }
  
  // push process exit event
  dmn_mac_push_event_exit_process(arena, events, process);
  
  // release process
  dmn_mac_process_release(process);
}

internal void
dmn_mac_event_load_module(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, U64 load_address, U64 name_vaddr)
{
  DMN_MAC_Module *module = dmn_mac_module_alloc(process, load_address, name_vaddr);
  dmn_mac_push_event_load_module(arena, events, process, module);
}

internal void
dmn_mac_event_unload_module(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, DMN_MAC_Module *module)
{
  dmn_mac_push_event_unload_module(arena, events, process, module);
  dmn_mac_module_release(process->ctx, module);
}

internal void
dmn_mac_event_breakpoint(Arena *arena, DMN_EventList *events, DMN_ActiveTrap *user_traps, pid_t tid)
{
  DMN_MAC_Thread  *thread  = dmn_mac_thread_from_pid(tid);
  DMN_MAC_Process *process = thread->process;
  U64              ip      = dmn_mac_thread_read_ip(thread);
  // TODO
  // is this user trap?
  DMN_ActiveTrap *hit_user_trap = 0;
  {
    DMN_Handle process_handle = dmn_mac_handle_from_process(process);
    for EachNode(active_trap, DMN_ActiveTrap, user_traps)
    {
      if(MemoryCompare(&active_trap->trap->process, &process_handle, sizeof(DMN_Handle)) == 0)
      {
        if(active_trap->trap->vaddr == ip-1)
        {
          hit_user_trap = active_trap;
          break;
        }
      }
    }
  }
  
  // is this a probe trap?
  DMN_MAC_ProbeType probe_type = DMN_MAC_ProbeType_Null;
  if(hit_user_trap == 0)
  {
    for EachNode(active_trap, DMN_ActiveTrap, process->ctx->first_probe_trap)
    {
      if(active_trap->trap->vaddr == ip-1)
      {
        probe_type = active_trap->trap->id;
        break;
      }
    }
  }

  if(probe_type == DMN_MAC_ProbeType_Null)
  {
    // rollback IP on user traps
    if(hit_user_trap)
    {
      U64 ip = dmn_mac_thread_read_ip(thread);
      dmn_mac_thread_write_ip(thread, ip - 1);
    }
    
    DMN_Event *e = dmn_event_list_push(arena, events);
    e->kind                = DMN_EventKind_Breakpoint;
    e->process             = dmn_mac_handle_from_process(process);
    e->thread              = dmn_mac_handle_from_thread(thread);
    e->instruction_pointer = dmn_mac_thread_read_ip(thread);
  }
}

internal void
dmn_mac_event_data_breakpoint(Arena *arena, DMN_EventList *events, pid_t tid)
{
  DMN_MAC_Thread *thread = dmn_mac_thread_from_pid(tid);
  
  B32 is_valid = 1;
  U64 address  = 0;
  switch(thread->process->ctx->arch)
  {
    case Arch_Null: {} break;
    case Arch_x64:
    {
      REGS_RegBlockX64 *regs_x64 = thread->reg_block;
      if(regs_x64->dr6.u64 & X64_DebugStatusFlag_B0)
      {
        address = regs_x64->dr0.u64;
      }
      else if(regs_x64->dr6.u64 & X64_DebugStatusFlag_B1)
      {
        address = regs_x64->dr1.u64;
      }
      else if(regs_x64->dr6.u64 & X64_DebugStatusFlag_B2)
      {
        address = regs_x64->dr2.u64;
      }
      else if(regs_x64->dr6.u64 & X64_DebugStatusFlag_B3)
      {
        address = regs_x64->dr3.u64;
      }
      else
      {
        is_valid = 0;
      }
    } break;
    case Arch_x86:
    case Arch_arm32:
    case Arch_arm64:
    { NotImplemented; } break;
    default: { InvalidPath; } break;
  }
  
  if(is_valid)
  {
    dmn_mac_push_event_breakpoint(arena, events, thread, address);
  }
}

internal void
dmn_mac_event_halt(Arena *arena, DMN_EventList *events)
{
  dmn_mac_push_event_halt(arena, events);
}

internal void
dmn_mac_event_single_step(Arena *arena, DMN_EventList *events, pid_t tid)
{
  DMN_MAC_Thread *thread = dmn_mac_thread_from_pid(tid);
  
  // clear single step flag
  dmn_mac_set_single_step_flag(thread, 0);
  
  // push event
  dmn_mac_push_event_single_step(arena, events, thread);
}

internal void
dmn_mac_event_exception(Arena *arena, DMN_EventList *events, pid_t tid, U64 signo)
{
  DMN_MAC_Thread *thread = dmn_mac_thread_from_pid(tid);
  
  thread->pass_through_signal = 1;
  thread->pass_through_signo  = signo;
  
  dmn_mac_push_event_exception(arena, events, thread, signo);
}

internal DMN_MAC_Process *
dmn_mac_event_attach(Arena *arena, DMN_EventList *events, pid_t pid)
{
  Temp scratch = scratch_begin(&arena, 1);
  
  // create process
  DMN_MAC_Process *process = dmn_mac_event_create_process(arena, events, pid, 0, DMN_MAC_CreateProcessFlag_DebugSubprocesses|DMN_MAC_CreateProcessFlag_Rebased);
  
  // handshake complete
  dmn_mac_push_event_handshake_complete(arena, events, process);
  
  scratch_end(scratch);
  return process;
}

////////////////////////////////
//~ rjf: @dmn_os_hooks Main Layer Initialization (Implemented Per-OS)

internal void
dmn_init(void)
{
  if(dmn_mac_state == 0)
  {
    local_persist DMN_MAC_State state;
    dmn_mac_state = &state;
    
    dmn_mac_state->arena          = arena_alloc();
    dmn_mac_state->access_mutex   = mutex_alloc();
    dmn_mac_state->entities_arena = arena_alloc(.reserve_size = GB(32), .commit_size = KB(64), .flags = ArenaFlag_NoChain);
    dmn_mac_state->entities_base  = push_array(dmn_mac_state->entities_arena, DMN_MAC_Entity, 0);
    dmn_mac_state->tid_ht         = hash_table_init(dmn_mac_state->arena, 0x2000);
    dmn_mac_state->pid_ht         = hash_table_init(dmn_mac_state->arena, 0x400);
    dmn_mac_state->halter_mutex   = mutex_alloc();
    dmn_mac_entity_alloc(DMN_MAC_EntityKind_Null);

    dmn_mac_state->exc_port = dmn_mac_make_exception_port();
    
    // find offsets of TLS index and TLS offset in the link_map struct
    // 
    // TODO: assuming that target is using same libc version as debugger
    {
      DMN_MAC_DbDesc *tls_modid_desc  = dlsym(RTLD_DEFAULT, "_thread_db_link_map_l_tls_modid");
      DMN_MAC_DbDesc *tls_offset_desc = dlsym(RTLD_DEFAULT, "_thread_db_link_map_l_tls_offset");
      if(tls_modid_desc && tls_offset_desc)
      {
        if(tls_modid_desc->bit_size <= 64 && tls_offset_desc->bit_size <= 64)
        {
          dmn_mac_state->tls_modid_desc  = *tls_modid_desc;
          dmn_mac_state->tls_offset_desc = *tls_offset_desc;
          dmn_mac_state->is_tls_detected = 1;
        }
        else { Assert(0 && "invalid TLS desc"); }
      }
    }
  }
  if(dmn_mac_exception_state == 0)
  {
    local_persist DMN_MAC_ExceptionState state;
    dmn_mac_exception_state = &state;
    
    dmn_mac_exception_state->arena = arena_alloc();
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
  MutexScope(dmn_mac_state->access_mutex)
  {
    dmn_mac_state->access_run_state = 1;
  }
}

internal void
dmn_ctrl_exclusive_access_end(void)
{
  MutexScope(dmn_mac_state->access_mutex)
  {
    dmn_mac_state->access_run_state = 0;
  }
}

internal U32
dmn_ctrl_launch(DMN_CtrlCtx *ctx, OS_ProcessLaunchParams *params)
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
  U64    envc = os_mac_state.default_env_count + params->env.node_count + 1;
  char **envp = push_array(scratch.arena, char *, envc);
  {
    // copy default environment
    MemoryCopyTyped(envp, os_mac_state.default_env, os_mac_state.default_env_count);
    
    // copy user environment
    U64 idx = os_mac_state.default_env_count;
    for EachNode(n, String8Node, params->env.first)
    {
      envp[idx] = (char *)str8_copy(scratch.arena, n->string).str;
      idx += 1;
    }
  }
  
  // create zero-terminated work directory path
  char *work_dir_path = (char *)str8_copy(scratch.arena, params->path).str;
  
  // fork process
  pid_t pid = fork();
  
  // child process
  if(pid == 0)
  {
    // Wait for the debugger to attach to the process
    if(task_suspend(mach_task_self()) != 0) { goto child_exit; }

    // change work directory to tracee
    if(OS_MAC_RETRY_ON_EINTR(chdir(work_dir_path)) < 0) { goto child_exit; }

    // replace process with target program
    if(OS_MAC_RETRY_ON_EINTR(execve(argv[0], argv, envp)) < 0) { goto child_exit; }
    
    child_exit:;
    exit(0);
  }
  // parent process
  else if(pid > 0)
  {
    task_t task = 0;
    kern_return_t status_code = task_for_pid(mach_task_self(), pid, &task);
    
    if(status_code != 0)
    {
      fprintf(stderr, "failed to call task_for_pid on the child process: %s", mach_error_string(status_code));
      kill(pid, SIGTERM);
      return 0;
    }
    
    DMN_MAC_Process* process = dmn_mac_process_alloc(pid, DMN_MAC_ProcessState_Launch, 0, params->debug_subprocesses, 0);
    process->ctx = dmn_mac_process_ctx_alloc(process, 0);

    ptrace(PT_ATTACHEXC, pid, 0, 0);

    status_code = task_resume(task);
    if(status_code != 0)
    {
      fprintf(stderr, "failed to resume the child process: %s\n", mach_error_string(status_code));
    }
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
      dmn_mac_process_alloc(pid, DMN_MAC_ProcessState_Attach, 0, 1, 0);
      is_attached = 1;
    }
  }
  return is_attached;
}

internal B32
dmn_ctrl_kill(DMN_CtrlCtx *ctx, DMN_Handle process_handle, U32 exit_code)
{
  B32 result = 0;
  mutex_take(dmn_mac_state->halter_mutex);
  DMN_MAC_Process *process = dmn_mac_process_from_handle(process_handle);
  if(process)
  {
    kern_return_t status_code = task_terminate(process->task) == 0;
    result = OS_MAC_RETRY_ON_EINTR(kill(process->pid, SIGKILL)) >= 0;
    result = OS_MAC_RETRY_ON_EINTR(ptrace(PT_KILL, process->pid, 0, 0)) >= 0;
  }
  mutex_drop(dmn_mac_state->halter_mutex);
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
  
  mutex_take(dmn_mac_state->halter_mutex);
  
  // wait for signals from the running threads
  if(dmn_mac_state->process_count > 0)
  {
    // write traps to memory
    DMN_ActiveTrap *active_trap_first = 0, *active_trap_last = 0;
    {
      HashTable *process_ht = hash_table_init(scratch.arena, dmn_mac_state->process_count);
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
          DMN_ActiveTrap *is_set = hash_table_search_u64_raw(active_trap_ht, trap->vaddr);
          if(is_set) { continue; }
          
          // TODO: ctrl sends down traps for exited process
          DMN_MAC_Process *process = dmn_mac_process_from_handle(trap->process);
          if(!process) { continue; }
          
          // trap instruction
          DMN_ActiveTrap *active_trap = dmn_set_trap(scratch.arena, trap);
          
          // add trap to the active list
          SLLQueuePush(active_trap_first, active_trap_last, active_trap);
          
          // add (address -> trap)
          hash_table_push_u64_raw(scratch.arena, active_trap_ht, trap->vaddr, active_trap);
        }
      }
    }
    
    // enable single stepping
    if(!dmn_handle_match(ctrls->single_step_thread, dmn_handle_zero()))
    {
      DMN_MAC_Thread *single_step_thread = dmn_mac_thread_from_handle(ctrls->single_step_thread);
      if(single_step_thread)
      {
        dmn_mac_set_single_step_flag(single_step_thread, 1);
      }
      else
      {
        Assert(0 && "invalid single_step_thread handle");
      }
    }
    
    // schedule threads to run
    DMN_MAC_ThreadPtrList running_threads = {0};
    {
      for EachNode(process, DMN_MAC_Process, dmn_mac_state->first_process)
      {
        //- rjf: determine if this process is frozen
        B32 process_is_frozen = dmn_mac_state->is_halting;
        if(ctrls->run_entities_are_processes)
        {
          for EachIndex(idx, ctrls->run_entity_count)
          {
            if(dmn_handle_match(ctrls->run_entities[idx], dmn_mac_handle_from_process(process)))
            {
              process_is_frozen = 1;
              break;
            }
          }
        }
        
        for EachNode(thread, DMN_MAC_Thread, process->first_thread)
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
                if(dmn_handle_match(ctrls->run_entities[idx], dmn_mac_handle_from_thread(thread)))
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
            is_frozen = !dmn_handle_match(dmn_mac_handle_from_thread(thread), ctrls->single_step_thread);
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
              // AssertAlways(thread->state == DMN_MAC_ThreadState_Stopped);
              
              // write registers
              if(thread->is_reg_block_dirty)
              {
                thread->is_reg_block_dirty = !dmn_mac_thread_write_reg_block(thread);
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
    for EachNode(n, DMN_MAC_ThreadPtrNode, running_threads.first)
    {
      hash_table_push_u64_raw(scratch.arena, running_threads_ht, n->v->tid, n);
    }
    
    B32                   is_halt_done    = 0;
    DMN_MAC_ThreadPtrList stopped_threads = {0};
    do
    {
      DMN_MAC_ExceptionResult result;
      {
        mutex_drop(dmn_mac_state->halter_mutex);
        result = dmn_mac_wait_for_exception(dmn_mac_state->exc_port);
        mutex_take(dmn_mac_state->halter_mutex);
      }
      
      if(result.timed_out)
      {
        // NOTE(yuraiz): As I understand task_suspend is reliable enough
        if(dmn_mac_state->is_halting)
        {
          is_halt_done = 1;
          break;
        }

        for EachNode(process, DMN_MAC_Process, dmn_mac_state->first_process)
        {
          //////////////////////////
          //-yuraiz monitor threads
          //
          thread_act_array_t threads = NULL;
          mach_msg_type_number_t threads_len = 0;
          task_threads(process->task, &threads, &threads_len);

          // generate exit thread events
          for EachNode(thread, DMN_MAC_Thread, process->first_thread)
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
              dmn_mac_event_exit_thread(arena, &events, thread->tid, 0);
            }
          }

          // generate new thread events
          for EachIndex(i, threads_len)
          {
            B32 exists = 0;
            for EachNode(thread, DMN_MAC_Thread, process->first_thread)
            {
              if(thread->tid == threads[i])
              {
                exists = 1;
              }
            }

            if(!exists)
            {
              dmn_mac_event_create_thread(arena, &events, process, threads[i]);
            }
          }

          vm_deallocate(mach_task_self(), (vm_address_t)threads, threads_len * sizeof(threads[0]));

          ////////////////////
          //- yuraiz: monitor modules
          //
          DMN_MAC_ImageArray images = dmn_mac_read_dyld_images(scratch.arena, process);

          // generate unload module events
          for EachNode(module, DMN_MAC_Module, process->ctx != 0 ? process->ctx->first_module : 0)
          {
            B32 exists = 0;
            for EachIndex(i, images.count)
            {
              U64 load_address = (U64)images.items[i].imageLoadAddress;
              if(module->base_vaddr == load_address)
              {
                exists = 1;
              }
            }
            if(!exists)
            {
              dmn_mac_event_unload_module(arena, &events, process, module);
            }
          }

          // generate load module events
          for EachIndex(i, images.count)
          {
            U64 load_address = (U64)images.items[i].imageLoadAddress;
            U64 name_vaddr   = (U64)images.items[i].imageFilePath;

            B32 exists = 0;
            for EachNode(module, DMN_MAC_Module, process->ctx->first_module)
            {
              if(module->base_vaddr == load_address)
              {
                exists = 1;
              }
            }

            if(!exists)
            {
              dmn_mac_event_load_module(arena, &events, process, load_address, name_vaddr);
            }
          }
        }

        continue;
      }

      // intercept initing processes
      {
        pid_t pid = 0;
        kern_return_t status_code = pid_for_task(result.task, &pid);
        DMN_MAC_Process *process = dmn_mac_process_from_pid(pid);
        
        if(process && process->state != DMN_MAC_ProcessState_Normal)
        {
          switch(process->state)
          {
            default: continue;
            case DMN_MAC_ProcessState_Null:
            case DMN_MAC_ProcessState_Normal:
            {
              InvalidPath;
            } break;
            case DMN_MAC_ProcessState_Attach:
            {
              if(result.exception == EXC_SOFTWARE &&
                 result.code == EXC_SOFT_SIGNAL &&
                 (result.subcode == SIGSTOP))
              {
                if(task_resume(result.task) == 0)
                {
                  process->state = DMN_MAC_ProcessState_Normal;
                  goto wait_for_signal;
                }
                else { Assert(0 && "failed to resume tracee"); }
              }
              else { Assert(0 && "unexpected signal"); }
            } break;
            case DMN_MAC_ProcessState_Launch:
            {
              if(result.exception == EXC_SOFTWARE &&
                 result.code == EXC_SOFT_SIGNAL &&
                 (result.subcode == SIGSTOP))
              {
                if(task_resume(result.task) == 0)
                {
                  process->state = DMN_MAC_ProcessState_WaitForExec;
                  goto wait_for_signal;
                }
                else { Assert(0 && "failed to resume tracee"); }
              }
              else { Assert(0 && "unexpected signal"); }
            } break;
            case DMN_MAC_ProcessState_WaitForExec:
            {
              if(result.exception == EXC_SOFTWARE &&
                 result.code == EXC_SOFT_SIGNAL &&
                 result.subcode == SIGTRAP)
              {
                if(task_resume(result.task) == 0)
                {
                  DMN_MAC_CreateProcessFlags create_flags = process->debug_subprocesses ? DMN_MAC_CreateProcessFlag_DebugSubprocesses : 0;
                  dmn_mac_process_release(process);
                  process = dmn_mac_event_create_process(arena, &events, pid, 0, create_flags);

                  process->state = DMN_MAC_ProcessState_Normal;

                  thread_act_array_t threads = NULL;
                  mach_msg_type_number_t threads_len = 0;
                  task_threads(process->task, &threads, &threads_len);
                  for EachIndex(i, threads_len)
                  {
                    dmn_mac_event_create_thread(arena, &events, process, threads[i]);
                  }

                  goto wait_for_signal;
                }
                else { Assert(0 && "failed to resume tracee"); }
              }
              else { Assert(0 && "unexpected signal"); }
            } break;
          }
          wait_for_signal:;
          continue;
        }
      }
      
      // TODO(yuriaiz)

      if(result.exception == EXC_BREAKPOINT)
      {
        // TODO(yuraiz): handle different types of breakpoints
        pid_t pid = 0;
        pid_for_task(result.task, &pid);
        DMN_MAC_Thread *thread = dmn_mac_thread_from_pid(pid);
        dmn_mac_push_event_single_step(arena, &events, thread);
        break;
      }
      

      //   {
   
      //   }
      //   else
      //   {
      //     dmn_mac_event_exception(arena, &events, wait_id, wstopsig);
      //   }
      // }
      // else { Assert(0 && "unexpected stop code"); }
    } while(running_threads.count > 0 || dmn_mac_state->process_pending_creation > 0 || dmn_mac_state->threads_pending_creation > 0);
    
    // finalize halter state
    if(is_halt_done)
    {
      // push event
      dmn_mac_event_halt(arena, &events);
      
      // reset state
      dmn_mac_state->halter_tid     = 0;
      dmn_mac_state->halt_code      = 0;
      dmn_mac_state->halt_user_data = 0;
      dmn_mac_state->is_halting     = 0;
    }
    
    // restore original instruction bytes
    for EachNode(active_trap, DMN_ActiveTrap, active_trap_first)
    {
      // skip process that exited during the wait
      DMN_MAC_Process *process = dmn_mac_process_from_handle(active_trap->trap->process);
      if(!process) { continue; }
      
      if(!dmn_process_write(active_trap->trap->process, r1u64(active_trap->trap->vaddr, active_trap->trap->vaddr + active_trap->swap_bytes.size), active_trap->swap_bytes.str))
      {
        Assert(0 && "failed to restore original instruction bytes");
      }
    }
  }
  
  if(events.count == 0 && dmn_mac_state->process_count == 0)
  {
    dmn_mac_push_event_not_attached(arena, &events);
  }
  
  mutex_drop(dmn_mac_state->halter_mutex);
  scratch_end(scratch);
  return events;
}

////////////////////////////////
//~ rjf: @dmn_os_hooks Halting (Implemented Per-OS)

internal void
dmn_halt(U64 code, U64 user_data)
{
  mutex_take(dmn_mac_state->halter_mutex);
  if(dmn_mac_state->process_count)
  {
    if(dmn_mac_state->process_count)
    {
      dmn_mac_state->halter_tid     = pthread_mach_thread_np(pthread_self());
      dmn_mac_state->halt_code      = code;
      dmn_mac_state->halt_user_data = user_data;
      dmn_mac_state->is_halting     = 1;
      for EachNode(process, DMN_MAC_Process, dmn_mac_state->first_process)
      {
        task_suspend(process->task);
      }
    }
  }
  mutex_drop(dmn_mac_state->halter_mutex);
}

////////////////////////////////
//~ rjf: @dmn_os_hooks Introspection Functions (Implemented Per-OS)

//- rjf: non-blocking-control-thread access barriers

internal B32
dmn_access_open(void)
{
  B32 result = 0;
  if(dmn_mac_ctrl_thread)
  {
    result = 1;
  }
  else
  {
    mutex_take(dmn_mac_state->access_mutex);
    result = !dmn_mac_state->access_run_state;
  }
  return result;
}

internal void
dmn_access_close(void)
{
  if(!dmn_mac_ctrl_thread)
  {
    mutex_drop(dmn_mac_state->access_mutex);
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
dmn_process_memory_protect(DMN_Handle process, U64 vaddr, U64 size, OS_AccessFlags flags)
{
  // TODO(yuraiz)
}

internal U64
dmn_process_read(DMN_Handle process_handle, Rng1U64 range, void *dst)
{
  DMN_MAC_Process *process = dmn_mac_process_from_handle(process_handle);
  return dmn_mac_process_read(process, range, dst);
}

internal B32
dmn_process_write(DMN_Handle process_handle, Rng1U64 range, void *src)
{
  DMN_MAC_Process *process = dmn_mac_process_from_handle(process_handle);
  return dmn_mac_process_write(process, range, src);
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
  DMN_MAC_Thread *thread = dmn_mac_thread_from_handle(thread_handle);
  if(thread)
  {
    // TODO(yuraiz): maybe stackbottom is what's actually needed here?;
    result = thread->stackaddr;
  }
  return result;
}

internal U64
dmn_tls_root_vaddr_from_thread(DMN_Handle thread_handle)
{
  // TODO(yuraiz)
  B32 result = 0;
  DMN_MAC_Thread *thread = dmn_mac_thread_from_handle(thread_handle);
  if(thread)
  {
    // TODO(yuraiz): understand the offset
    result = thread->thread_local_base;
  }
  return result;
}

internal B32
dmn_thread_read_reg_block(DMN_Handle thread_handle, void *reg_block)
{
  B32 result = 0;
  DMN_AccessScope
  {
    DMN_MAC_Thread *thread = dmn_mac_thread_from_handle(thread_handle);
    if(thread)
    {
      U64 reg_block_size = regs_block_size_from_arch(Arch_arm64);
      MemoryCopy(reg_block, thread->reg_block, reg_block_size);
      result = 1;
    }
  }
  return result;
}

internal B32
dmn_thread_write_reg_block(DMN_Handle thread_handle, void *reg_block)
{
  B32 result = 0;
  DMN_AccessScope
  {
    DMN_MAC_Thread *thread = dmn_mac_thread_from_handle(thread_handle);
    if(thread)
    {
      U64 reg_block_size = regs_block_size_from_arch(thread->process->ctx->arch);
      MemoryCopy(thread->reg_block, reg_block, reg_block_size);
      thread->is_reg_block_dirty = 1;
      result = 1;
    }
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
