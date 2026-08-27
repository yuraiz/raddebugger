// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MACOS_DEMON_H
#define MACOS_DEMON_H

////////////////////////////////
//~ Includes

#include <sys/ptrace.h>
#include <mach/arm/thread_status.h>

// /Users/yuraiz/Projects/raddebugger/src/macos/demon/demon_os_mac.c:70:12
#include "macos/demon/macos_demon_exc.h"

////////////////////////////////
//~ TLS

typedef struct MAC_DMN_DbDesc
{
  U32 bit_size;
  U32 count;
  U32 offset;
} MAC_DMN_DbDesc;

////////////////////////////////
//~ SDT Probes

typedef struct MAC_DMN_Probe
{
  String8       provider;
  String8       name;
  String8       args_string;
  STAP_ArgArray args;
  U64           pc;
  U64           semaphore;
} MAC_DMN_Probe;

typedef struct MAC_DMN_ProbeNode
{
  MAC_DMN_Probe v;
  struct MAC_DMN_ProbeNode *next;
} MAC_DMN_ProbeNode;

typedef struct MAC_DMN_ProbeList
{
  U64                count;
  MAC_DMN_ProbeNode *first;
  MAC_DMN_ProbeNode *last;
} MAC_DMN_ProbeList;

#define MAC_DMN_Probe_XList             \
  X(InitStart,     2, "init_start")     \
  X(InitComplete,  2, "init_complete")  \
  X(RelocStart,    2, "reloc_start")    \
  X(RelocComplete, 3, "reloc_complete") \
  X(MapStart,      2, "map_start")      \
  X(MapComplete,   3, "map_complete")   \
  X(UnmapStart,    2, "unmap_start")    \
  X(UnmapComplete, 2, "unmap_complete") \
  X(LongJmp,       3, "longjmp")        \
  X(LongJmpTarget, 3, "longjmp_target") \
  X(SetJmp,        3, "setjmp")

typedef enum
{
  MAC_DMN_ProbeType_Null,
#define X(_N,...) MAC_DMN_ProbeType_##_N,
  MAC_DMN_Probe_XList
#undef X
  MAC_DMN_ProbeType_Count,
} MAC_DMN_ProbeType;

////////////////////////////////
//~ Process Info

typedef struct MAC_DMN_Auxv
{
  U64 base;
  U64 phnum;
  U64 phent;
  U64 phdr;
  U64 execfn;
  U64 pagesz;
} MAC_DMN_Auxv;

typedef struct MAC_DMN_DynamicInfo
{
  U64 hash_vaddr;
  U64 gnu_hash_vaddr;
  U64 strtab_vaddr;
  U64 strtab_size;
  U64 symtab_vaddr;
  U64 symtab_entry_size;
} MAC_DMN_DynamicInfo;

////////////////////////////////
//~ Entities

typedef enum MAC_DMN_EntityKind
{
  MAC_DMN_EntityKind_Null,
  MAC_DMN_EntityKind_Process,
  MAC_DMN_EntityKind_ProcessCtx,
  MAC_DMN_EntityKind_Thread,
  MAC_DMN_EntityKind_Module,
} MAC_DMN_EntityKind;

typedef enum MAC_DMN_ThreadState
{
  MAC_DMN_ThreadState_Null,
  MAC_DMN_ThreadState_Running,
  MAC_DMN_ThreadState_Stopped,
  MAC_DMN_ThreadState_Exited,
  MAC_DMN_ThreadState_PendingCreation,
} MAC_DMN_ThreadState;

// Matches layout of arm_debug_state64
typedef struct MAC_DMN_ThreadDebugRegs
{
  U64 bvr[16];
  U64 bcr[16];
  U64 wvr[16];
  U64 wcr[16];
  U64 mdscr_el1;
} MAC_DMN_ThreadDebugRegs;

typedef struct MAC_DMN_Thread
{
  thread_act_t            tid;
  MAC_DMN_ThreadState     state;
  struct MAC_DMN_Process *process;
  void                   *reg_block;
  // TODO(yuraiz): I would put it in reg_block, but it isn't in debug info
  MAC_DMN_ThreadDebugRegs debug_regs;
  B32                     hit_hardware_breakpoint;
  B32                     clear_single_step;
  B32                     is_reg_block_dirty;
  B32                     pass_through_signal;
  U64                     pass_through_signo;
  U64                     orig_rax;
  U64                     thread_local_base;
  // NOTE(yuraiz): Those match internal pthread_s values
  U64                     stackaddr;
  U64                     stackbottom;
  struct MAC_DMN_Thread *next;
  struct MAC_DMN_Thread *prev;
} MAC_DMN_Thread;

typedef struct MAC_DMN_ThreadPtrNode
{
  MAC_DMN_Thread *v;
  struct MAC_DMN_ThreadPtrNode *next;
  struct MAC_DMN_ThreadPtrNode *prev;
} MAC_DMN_ThreadPtrNode;

typedef struct MAC_DMN_ThreadPtrList
{
  U64                    count;
  MAC_DMN_ThreadPtrNode *first;
  MAC_DMN_ThreadPtrNode *last;
} MAC_DMN_ThreadPtrList;

typedef struct MAC_DMN_Module
{
  U64 name_vaddr;
  U64 base_vaddr;
  U64 name_space_id;
  U64 size;
  U64 phvaddr;
  U64 phentsize;
  U64 phcount;
  U64 tls_index;
  U64 tls_offset;
  B8  is_live;
  B8  is_main;

  Guid guid;
  Rng1U64 unwind_info_range;
  Rng1U64 eh_frame_range;
  
  struct MAC_DMN_Module *next;
  struct MAC_DMN_Module *prev;
} MAC_DMN_Module;

typedef struct MAC_DMN_ModulePtrNode
{
  MAC_DMN_Module *v;
  struct MAC_DMN_ModulePtrNode *next;
} MAC_DMN_ModulePtrNode;

typedef struct MAC_DMN_ModulePtrList
{
  U64                    count;
  MAC_DMN_ModulePtrNode *first;
  MAC_DMN_ModulePtrNode *last;
} MAC_DMN_ModulePtrList;

typedef enum
{
  MAC_DMN_ProcessState_Null,
  MAC_DMN_ProcessState_Launch,
  MAC_DMN_ProcessState_Attach,
  MAC_DMN_ProcessState_WaitForExec,
  MAC_DMN_ProcessState_Normal,
} MAC_DMN_ProcessState;

typedef enum
{
  MAC_DMN_CreateProcessFlag_DebugSubprocesses = (1 << 0),
  MAC_DMN_CreateProcessFlag_Rebased           = (1 << 1),
  MAC_DMN_CreateProcessFlag_Cow               = (1 << 2),
  MAC_DMN_CreateProcessFlag_ClonedMemory      = (1 << 3),
} MAC_DMN_CreateProcessFlags;

typedef struct MAC_DMN_Process
{
  pid_t                      pid;
  task_t                     task;
  int                        fd;
  MAC_DMN_ProcessState       state;
  B32                        debug_subprocesses;
  B32                        is_cow;
  B32                        vfork_with_spoof;
  U64                        dyld_move_vaddr;
  U64                        dyld_name_vaddr;
  U64                        thread_count;
  MAC_DMN_Thread            *first_thread;
  MAC_DMN_Thread            *last_thread;
  U64                        main_thread_exit_code;
  struct MAC_DMN_Process    *parent_process;
  struct MAC_DMN_ProcessCtx *ctx;

  struct MAC_DMN_Process *next;
  struct MAC_DMN_Process *prev;
} MAC_DMN_Process;

typedef struct MAC_DMN_ProcessPtrNode
{
  MAC_DMN_Process *v;
  struct MAC_DMN_ProcessPtrNode *next;
} MAC_DMN_ProcessPtrNode;

typedef struct MAC_DMN_ProcessPtrList
{
  U64                     count;
  MAC_DMN_ProcessPtrNode *first;
  MAC_DMN_ProcessPtrNode *last;
} MAC_DMN_ProcessPtrList;

typedef struct MAC_DMN_ActiveTrap MAC_DMN_ActiveTrap;
struct MAC_DMN_ActiveTrap
{
  MAC_DMN_ActiveTrap *next;
  B32 good;
  DMN_Trap *trap;
  String8 swap_bytes;
};


typedef struct MAC_DMN_ProcessCtx
{
  Arena                 *arena;
  Arch                   arch;
  HashTable             *loaded_modules_ht;
  MAC_DMN_Probe        **probes;
  MAC_DMN_ActiveTrap    *first_probe_trap;
  MAC_DMN_ActiveTrap    *last_probe_trap;
  // NOTE(we set a trap to that function, and dyld calls it when images are loaded or unloaded)
  U64                    dyld_notifier_address;
  MAC_DMN_Module        *first_module;
  MAC_DMN_Module        *last_module;
  U64                    module_count;
  U64                    ref_count;

  String8List free_reg_blocks;
  String8List free_reg_block_nodes;
} MAC_DMN_ProcessCtx;

typedef struct MAC_DMN_Entity
{
  union
  {
    MAC_DMN_Process    process;
    MAC_DMN_ProcessCtx process_ctx;
    MAC_DMN_Thread     thread;
    MAC_DMN_Module     module;
    struct
    {
      struct MAC_DMN_Entity *next;
    };
  };
  U32                gen;
  MAC_DMN_EntityKind kind;
} MAC_DMN_Entity;

typedef struct MAC_DMN_EntityNode
{
  MAC_DMN_Entity *v;
  struct MAC_DMN_EntityNode *next;
} MAC_DMN_EntityNode;

typedef struct MAC_DMN_EntityList
{
  U64                 count;
  MAC_DMN_EntityNode *first;
  MAC_DMN_EntityNode *last;
} MAC_DMN_EntityList;

////////////////////////////////
//~ Global State

typedef struct MAC_DMN_State
{
  Arena *arena;

  // yuraiz: mach exception handling
  mach_port_t exc_port; 

  // rjf: access locking mechanism
  Mutex access_mutex;
  B32   access_run_state;

  // rjf: entity storage
  Arena          *entities_arena;
  MAC_DMN_Entity *entities_base;
  MAC_DMN_Entity *free_entity;
  U64             entities_count;

  HashTable *tid_ht; // thread id -> thread entity
  HashTable *pid_ht; // process id -> process entity

  // process tracking
  U64              process_count;
  MAC_DMN_Process *first_process;
  MAC_DMN_Process *last_process;

  // process/thread creation tracking
  U64 process_pending_creation;
  U64 threads_pending_creation;

  // halter
  Mutex halter_mutex;
  pid_t halter_tid;
  U64   halt_code;
  U64   halt_user_data;
  B32   is_halting;

  // TLS
  B32            is_tls_detected;
  MAC_DMN_DbDesc tls_modid_desc;
  MAC_DMN_DbDesc tls_offset_desc;
} MAC_DMN_State;

////////////////////////////////
//~ rjf: Globals

global MAC_DMN_State *mac_dmn_state = 0;
thread_static B32 mac_dmn_ctrl_thread = 0;

////////////////////////////////
//~ rjf: Helpers

internal MAC_DMN_Entity *     mac_dmn_entity_alloc(MAC_DMN_EntityKind kind);
internal MAC_DMN_Process *    mac_dmn_process_alloc(pid_t pid, MAC_DMN_ProcessState state, MAC_DMN_Process *parent_process, B32 debug_subprocesses, B32 is_cow);
internal DMN_Handle           mac_dmn_handle_from_entity(MAC_DMN_Entity *entity);
internal DMN_Handle           mac_dmn_handle_from_process(MAC_DMN_Process *process);
internal DMN_Handle           mac_dmn_handle_from_process_ctx(MAC_DMN_ProcessCtx *process_ctx);
internal DMN_Handle           mac_dmn_handle_from_thread(MAC_DMN_Thread *thread);
internal DMN_Handle           mac_dmn_handle_from_module(MAC_DMN_Module *module);
internal MAC_DMN_Entity *     mac_dmn_entity_from_handle(DMN_Handle handle, MAC_DMN_EntityKind expected_kind);
internal MAC_DMN_Process *    mac_dmn_process_from_handle(DMN_Handle process_handle);
internal MAC_DMN_ProcessCtx * mac_dmn_process_ctx_from_handle(DMN_Handle process_ctx_handle);
internal MAC_DMN_Thread *     mac_dmn_thread_from_handle(DMN_Handle thread_handle);
internal MAC_DMN_Module *     mac_dmn_module_from_handle(DMN_Handle module_handle);

internal void                 mac_dmn_push_event_create_process(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process);
internal void                 mac_dmn_push_event_exit_process(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process);
internal void                 mac_dmn_push_event_create_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread);
internal void                 mac_dmn_push_event_exit_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread, U64 exit_code);
internal void                 mac_dmn_push_event_load_module(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, MAC_DMN_Module *module);
internal void                 mac_dmn_push_event_unload_module(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, MAC_DMN_Module *module);
internal void                 mac_dmn_push_event_handshake_complete(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process);
internal void                 mac_dmn_push_event_breakpoint(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread, U64 address);
internal void                 mac_dmn_push_event_single_step(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread);
internal void                 mac_dmn_push_event_exception(Arena *arena, DMN_EventList *events, MAC_DMN_Thread *thread, U64 signo);
internal void                 mac_dmn_push_event_halt(Arena *arena, DMN_EventList *events);
internal void                 mac_dmn_push_event_not_attached(Arena *arena, DMN_EventList *events);

internal MAC_DMN_Thread *     mac_dmn_event_create_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, thread_act_t tid);
internal void                 mac_dmn_event_exit_thread(Arena *arena, DMN_EventList *events, pid_t tid, U64 exit_code);
internal MAC_DMN_Process *    mac_dmn_event_create_process(Arena *arena, DMN_EventList *events, pid_t pid, MAC_DMN_Process *parent_process, MAC_DMN_CreateProcessFlags flags);
internal void                 mac_dmn_event_exit_process(Arena *arena, DMN_EventList *events, pid_t pid);
internal void                 mac_dmn_event_load_module(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, U64 name_space_id, U64 new_link_map_vaddr);
internal void                 mac_dmn_event_unload_module(Arena *arena, DMN_EventList *events, MAC_DMN_Process *process, MAC_DMN_Module *module);
internal void                 mac_dmn_event_breakpoint(Arena *arena, DMN_EventList *events, MAC_DMN_ActiveTrap *user_traps, pid_t tid);
internal void                 mac_dmn_event_data_breakpoint(Arena *arena, DMN_EventList *events, pid_t tid);
internal void                 mac_dmn_event_halt(Arena *arena, DMN_EventList *events);
internal void                 mac_dmn_event_single_step(Arena *arena, DMN_EventList *events, pid_t tid);
internal void                 mac_dmn_event_exception(Arena *arena, DMN_EventList *events, pid_t tid, U64 signo);
internal MAC_DMN_Process *    mac_dmn_event_attach(Arena *arena, DMN_EventList *events, pid_t pid);

#endif // MACOS_DEMON_H
