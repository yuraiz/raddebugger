// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DEMON_CORE_MAC_H
#define DEMON_CORE_MAC_H

////////////////////////////////
//~ Includes

#include <sys/ptrace.h>
#include <mach/arm/thread_status.h>

#include "demon_os_mac.h"

////////////////////////////////
//~ TLS

typedef struct DMN_MAC_DbDesc
{
  U32 bit_size;
  U32 count;
  U32 offset;
} DMN_MAC_DbDesc;

////////////////////////////////
//~ SDT Probes

typedef struct DMN_MAC_Probe
{
  String8       provider;
  String8       name;
  String8       args_string;
  STAP_ArgArray args;
  U64           pc;
  U64           semaphore;
} DMN_MAC_Probe;

typedef struct DMN_MAC_ProbeNode
{
  DMN_MAC_Probe v;
  struct DMN_MAC_ProbeNode *next;
} DMN_MAC_ProbeNode;

typedef struct DMN_MAC_ProbeList
{
  U64                count;
  DMN_MAC_ProbeNode *first;
  DMN_MAC_ProbeNode *last;
} DMN_MAC_ProbeList;

#define DMN_MAC_Probe_XList             \
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
  DMN_MAC_ProbeType_Null,
#define X(_N,...) DMN_MAC_ProbeType_##_N,
  DMN_MAC_Probe_XList
#undef X
  DMN_MAC_ProbeType_Count,
} DMN_MAC_ProbeType;

////////////////////////////////
//~ Process Info

typedef struct DMN_MAC_ImageArray
{
  struct dyld_image_info *items;
  U64 count;
} DMN_MAC_ImageArray;

typedef struct DMN_MAC_Auxv
{
  U64 base;
  U64 phnum;
  U64 phent;
  U64 phdr;
  U64 execfn;
  U64 pagesz;
} DMN_MAC_Auxv;

typedef struct DMN_MAC_DynamicInfo
{
  U64 hash_vaddr;
  U64 gnu_hash_vaddr;
  U64 strtab_vaddr;
  U64 strtab_size;
  U64 symtab_vaddr;
  U64 symtab_entry_size;
} DMN_MAC_DynamicInfo;

////////////////////////////////
//~ Entities

typedef enum DMN_MAC_EntityKind
{
  DMN_MAC_EntityKind_Null,
  DMN_MAC_EntityKind_Process,
  DMN_MAC_EntityKind_ProcessCtx,
  DMN_MAC_EntityKind_Thread,
  DMN_MAC_EntityKind_Module,
} DMN_MAC_EntityKind;

typedef enum DMN_MAC_ThreadState
{
  DMN_MAC_ThreadState_Null,
  DMN_MAC_ThreadState_Running,
  DMN_MAC_ThreadState_Stopped,
  DMN_MAC_ThreadState_Exited,
  DMN_MAC_ThreadState_PendingCreation,
} DMN_MAC_ThreadState;

typedef struct DMN_MAC_Thread
{
  thread_act_t            tid;
  DMN_MAC_ThreadState     state;
  struct DMN_MAC_Process *process;
  void                   *reg_block;
  // TODO(yuraiz): I would put it in reg_block, but it isn't in debug info
  U64                     reg_mdscr_el1;
  B32                     is_reg_block_dirty;
  B32                     pass_through_signal;
  U64                     pass_through_signo;
  U64                     orig_rax;
  U64                     thread_local_base;
  // NOTE(yuraiz): Those match internal pthread_s values
  U64                     stackaddr;
  U64                     stackbottom;
  struct DMN_MAC_Thread *next;
  struct DMN_MAC_Thread *prev;
} DMN_MAC_Thread;

typedef struct DMN_MAC_ThreadPtrNode
{
  DMN_MAC_Thread *v;
  struct DMN_MAC_ThreadPtrNode *next;
  struct DMN_MAC_ThreadPtrNode *prev;
} DMN_MAC_ThreadPtrNode;

typedef struct DMN_MAC_ThreadPtrList
{
  U64                    count;
  DMN_MAC_ThreadPtrNode *first;
  DMN_MAC_ThreadPtrNode *last;
} DMN_MAC_ThreadPtrList;

typedef struct DMN_MAC_Module
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

  struct DMN_MAC_Module *next;
  struct DMN_MAC_Module *prev;
} DMN_MAC_Module;

typedef struct DMN_MAC_ModulePtrNode
{
  DMN_MAC_Module *v;
  struct DMN_MAC_ModulePtrNode *next;
} DMN_MAC_ModulePtrNode;

typedef struct DMN_MAC_ModulePtrList
{
  U64                    count;
  DMN_MAC_ModulePtrNode *first;
  DMN_MAC_ModulePtrNode *last;
} DMN_MAC_ModulePtrList;

typedef enum
{
  DMN_MAC_ProcessState_Null,
  DMN_MAC_ProcessState_Launch,
  DMN_MAC_ProcessState_Attach,
  DMN_MAC_ProcessState_WaitForExec,
  DMN_MAC_ProcessState_Normal,
} DMN_MAC_ProcessState;

typedef enum
{
  DMN_MAC_CreateProcessFlag_DebugSubprocesses = (1 << 0),
  DMN_MAC_CreateProcessFlag_Rebased           = (1 << 1),
  DMN_MAC_CreateProcessFlag_Cow               = (1 << 2),
  DMN_MAC_CreateProcessFlag_ClonedMemory      = (1 << 3),
} DMN_MAC_CreateProcessFlags;

typedef struct DMN_MAC_Process
{
  pid_t                      pid;
  task_t                     task;
  int                        fd;
  DMN_MAC_ProcessState       state;
  B32                        debug_subprocesses;
  B32                        is_cow;
  B32                        vfork_with_spoof;
  U64                        thread_count;
  DMN_MAC_Thread            *first_thread;
  DMN_MAC_Thread            *last_thread;
  U64                        main_thread_exit_code;
  struct DMN_MAC_Process    *parent_process;
  struct DMN_MAC_ProcessCtx *ctx;

  struct DMN_MAC_Process *next;
  struct DMN_MAC_Process *prev;
} DMN_MAC_Process;

typedef struct DMN_MAC_ProcessPtrNode
{
  DMN_MAC_Process *v;
  struct DMN_MAC_ProcessPtrNode *next;
} DMN_MAC_ProcessPtrNode;

typedef struct DMN_MAC_ProcessPtrList
{
  U64                     count;
  DMN_MAC_ProcessPtrNode *first;
  DMN_MAC_ProcessPtrNode *last;
} DMN_MAC_ProcessPtrList;

typedef struct DMN_MAC_ProcessCtx
{
  Arena                 *arena;
  Arch                   arch;
  U64                    rdebug_vaddr;
  ELF_Class              dl_class;
  HashTable             *loaded_modules_ht;
  DMN_MAC_Probe        **probes;
  DMN_ActiveTrap        *first_probe_trap;
  DMN_ActiveTrap        *last_probe_trap;
  DMN_MAC_Module        *first_module;
  DMN_MAC_Module        *last_module;
  U64                    module_count;
  U64                    ref_count;

  String8List free_reg_blocks;
  String8List free_reg_block_nodes;
} DMN_MAC_ProcessCtx;

typedef struct DMN_MAC_Entity
{
  union
  {
    DMN_MAC_Process    process;
    DMN_MAC_ProcessCtx process_ctx;
    DMN_MAC_Thread     thread;
    DMN_MAC_Module     module;
    struct
    {
      struct DMN_MAC_Entity *next;
    };
  };
  U32                gen;
  DMN_MAC_EntityKind kind;
} DMN_MAC_Entity;

typedef struct DMN_MAC_EntityNode
{
  DMN_MAC_Entity *v;
  struct DMN_MAC_EntityNode *next;
} DMN_MAC_EntityNode;

typedef struct DMN_MAC_EntityList
{
  U64                 count;
  DMN_MAC_EntityNode *first;
  DMN_MAC_EntityNode *last;
} DMN_MAC_EntityList;

////////////////////////////////
//~ Global State

typedef struct DMN_MAC_State
{
  Arena *arena;

  // yuraiz: mach exception handling
  mach_port_t exc_port; 

  // rjf: access locking mechanism
  Mutex access_mutex;
  B32   access_run_state;

  // rjf: entity storage
  Arena          *entities_arena;
  DMN_MAC_Entity *entities_base;
  DMN_MAC_Entity *free_entity;
  U64             entities_count;

  HashTable *tid_ht; // thread id -> thread entity
  HashTable *pid_ht; // process id -> process entity

  // process tracking
  U64              process_count;
  DMN_MAC_Process *first_process;
  DMN_MAC_Process *last_process;

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
  DMN_MAC_DbDesc tls_modid_desc;
  DMN_MAC_DbDesc tls_offset_desc;
} DMN_MAC_State;

////////////////////////////////
//~ rjf: Globals

global DMN_MAC_State *dmn_mac_state = 0;
thread_static B32 dmn_mac_ctrl_thread = 0;

////////////////////////////////
//~ rjf: Helpers

internal DMN_MAC_Entity *     dmn_mac_entity_alloc(DMN_MAC_EntityKind kind);
internal DMN_MAC_Process *    dmn_mac_process_alloc(pid_t pid, DMN_MAC_ProcessState state, DMN_MAC_Process *parent_process, B32 debug_subprocesses, B32 is_cow);
internal DMN_Handle           dmn_mac_handle_from_entity(DMN_MAC_Entity *entity);
internal DMN_Handle           dmn_mac_handle_from_process(DMN_MAC_Process *process);
internal DMN_Handle           dmn_mac_handle_from_process_ctx(DMN_MAC_ProcessCtx *process_ctx);
internal DMN_Handle           dmn_mac_handle_from_thread(DMN_MAC_Thread *thread);
internal DMN_Handle           dmn_mac_handle_from_module(DMN_MAC_Module *module);
internal DMN_MAC_Entity *     dmn_mac_entity_from_handle(DMN_Handle handle, DMN_MAC_EntityKind expected_kind);
internal DMN_MAC_Process *    dmn_mac_process_from_handle(DMN_Handle process_handle);
internal DMN_MAC_ProcessCtx * dmn_mac_process_ctx_from_handle(DMN_Handle process_ctx_handle);
internal DMN_MAC_Thread *     dmn_mac_thread_from_handle(DMN_Handle thread_handle);
internal DMN_MAC_Module *     dmn_mac_module_from_handle(DMN_Handle module_handle);

internal void                 dmn_mac_push_event_create_process(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process);
internal void                 dmn_mac_push_event_exit_process(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process);
internal void                 dmn_mac_push_event_create_thread(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread);
internal void                 dmn_mac_push_event_exit_thread(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread, U64 exit_code);
internal void                 dmn_mac_push_event_load_module(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, DMN_MAC_Module *module);
internal void                 dmn_mac_push_event_unload_module(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, DMN_MAC_Module *module);
internal void                 dmn_mac_push_event_handshake_complete(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process);
internal void                 dmn_mac_push_event_breakpoint(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread, U64 address);
internal void                 dmn_mac_push_event_single_step(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread);
internal void                 dmn_mac_push_event_exception(Arena *arena, DMN_EventList *events, DMN_MAC_Thread *thread, U64 signo);
internal void                 dmn_mac_push_event_halt(Arena *arena, DMN_EventList *events);
internal void                 dmn_mac_push_event_not_attached(Arena *arena, DMN_EventList *events);

internal DMN_MAC_Thread *     dmn_mac_event_create_thread(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, thread_act_t tid);
internal void                 dmn_mac_event_exit_thread(Arena *arena, DMN_EventList *events, pid_t tid, U64 exit_code);
internal DMN_MAC_Process *    dmn_mac_event_create_process(Arena *arena, DMN_EventList *events, pid_t pid, DMN_MAC_Process *parent_process, DMN_MAC_CreateProcessFlags flags);
internal void                 dmn_mac_event_exit_process(Arena *arena, DMN_EventList *events, pid_t pid);
internal void                 dmn_mac_event_load_module(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, U64 name_space_id, U64 new_link_map_vaddr);
internal void                 dmn_mac_event_unload_module(Arena *arena, DMN_EventList *events, DMN_MAC_Process *process, DMN_MAC_Module *module);
internal void                 dmn_mac_event_breakpoint(Arena *arena, DMN_EventList *events, DMN_ActiveTrap *user_traps, pid_t tid);
internal void                 dmn_mac_event_data_breakpoint(Arena *arena, DMN_EventList *events, pid_t tid);
internal void                 dmn_mac_event_halt(Arena *arena, DMN_EventList *events);
internal void                 dmn_mac_event_single_step(Arena *arena, DMN_EventList *events, pid_t tid);
internal void                 dmn_mac_event_exception(Arena *arena, DMN_EventList *events, pid_t tid, U64 signo);
internal DMN_MAC_Process *    dmn_mac_event_attach(Arena *arena, DMN_EventList *events, pid_t pid);

#endif // DEMON_CORE_MAC_H

