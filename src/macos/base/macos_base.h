#ifndef MACOS_BASE_H
#define MACOS_BASE_H

////////////////////////////////
//~ rjf: Includes

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <copyfile.h>
#include <time.h>
#include <unistd.h>
#include <libproc.h>
#include <spawn.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <crt_externs.h>

#include <dispatch/dispatch.h>

#undef internal
#undef global
#include <mach/mach.h>
#define internal static
#define global static

typedef struct tm tm;
typedef struct timespec timespec;

#define MAC_PATH_MAX 1024

////////////////////////////////
//~ rjf: Linux Call Interruption Retry Helper

#define MAC_RETRY_ON_EINTR(expr)           \
(__extension__({                           \
__typeof__(expr) __ret;                    \
do {                                       \
__ret = (expr);                            \
} while ((__ret == -1) && errno == EINTR); \
__ret;                                     \
}))

////////////////////////////////
//~ brt: Process Iterator

typedef struct MAC_ProcessIter MAC_ProcessIter;
struct MAC_ProcessIter
{
  pid_t *pids;
  U64 pids_count;
};

////////////////////////////////
//~ rjf: File Iterator

typedef struct MAC_FileIter MAC_FileIter;
struct MAC_FileIter
{
  DIR *dir;
  struct dirent *dp;
  String8 path;
};
StaticAssert(sizeof(Member(FileIter, memory)) >= sizeof(MAC_FileIter), mac_file_iter_size_check);

////////////////////////////////
//~ rjf: Safe Call Handler Chain

typedef struct MAC_SafeCallChain MAC_SafeCallChain;
struct MAC_SafeCallChain
{
  MAC_SafeCallChain *next;
  ThreadEntryPointFunctionType *fail_handler;
  void *ptr;
};

////////////////////////////////
//~ rjf: Entities

typedef enum MAC_EntityKind
{
  MAC_EntityKind_Thread,
  MAC_EntityKind_Mutex,
  MAC_EntityKind_RWMutex,
  MAC_EntityKind_ConditionVariable,
  MAC_EntityKind_Barrier,
}
MAC_EntityKind;

typedef struct MAC_Entity MAC_Entity;
struct MAC_Entity
{
  MAC_Entity *next;
  MAC_EntityKind kind;
  union
  {
    struct
    {
      pthread_t handle;
      ThreadEntryPointFunctionType *func;
      void *ptr;
    } thread;
    pthread_mutex_t mutex_handle;
    pthread_rwlock_t rwmutex_handle;
    struct
    {
      pthread_cond_t cond_handle;
      pthread_mutex_t rwlock_mutex_handle;
    } cv;
    struct
    {
      U64 thread_count;
      U64 gen_idx;
      U64 checked_in_count;
      pthread_cond_t cond_handle;
      pthread_mutex_t mutex_handle;
    } barrier;
  };
};

////////////////////////////////
//~ rjf: State

typedef struct MAC_State MAC_State;
struct MAC_State
{
  Arena *arena;
  SystemInfo system_info;
  ProcessInfo process_info;
  pthread_mutex_t entity_mutex;
  Arena *entity_arena;
  MAC_Entity *entity_free;
  U64 default_env_count;
  char **default_env;
};

////////////////////////////////
//~ rjf: Globals

global MAC_State mac_state = {0};
thread_static MAC_SafeCallChain *mac_safe_call_chain = 0;

////////////////////////////////
//~ rjf: Helpers

internal DateTime mac_date_time_from_tm(tm in, U32 msec);
internal tm mac_tm_from_date_time(DateTime dt);
internal timespec mac_timespec_from_date_time(DateTime dt);
internal DenseTime mac_dense_time_from_timespec(timespec in);
internal FileProperties mac_file_properties_from_stat(struct stat *s);
internal void mac_safe_call_sig_handler(int x);

////////////////////////////////
//~ rjf: Entities

internal MAC_Entity *mac_entity_alloc(MAC_EntityKind kind);
internal void mac_entity_release(MAC_Entity *entity);

////////////////////////////////
//~ rjf: Thread Entry Point

internal void *mac_thread_entry_point(void *ptr);

#endif // MACOS_BASE_H
