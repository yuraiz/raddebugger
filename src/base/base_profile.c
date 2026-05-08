// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#if PROFILE_INSTRUMENTS
internal inline os_signpost_id_t
instruments_signpost_push( void )
{
  if (instruments_stack_size == 0)
  {
    instruments_stack_size = 4096;
    instruments_stack = os_reserve(instruments_stack_size*sizeof(os_signpost_id_t));
    os_commit(instruments_stack, instruments_stack_size*sizeof(os_signpost_id_t));
  }
  U64 signpost_idx = instruments_stack_idx;
  instruments_stack[signpost_idx] = os_signpost_id_generate(instruments_log);
  instruments_stack_idx += 1;
  return instruments_stack[signpost_idx];
}

internal inline os_signpost_id_t
instruments_signpost_pop( void )
{
  Assert(instruments_stack_idx > 0);
  instruments_stack_idx -= 1;
  return instruments_stack[instruments_stack_idx];
}
internal inline void
instruments_begin( char *fmt, ... )
{
  os_signpost_id_t signpost = instruments_signpost_push();
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 string = str8fv(scratch.arena, fmt, args);
  os_signpost_interval_begin(instruments_log, signpost, "Trace", "%s", string.str);
  va_end(args);
  scratch_end(scratch);
}

internal inline void
instruments_end( void )
{
  os_signpost_id_t signpost = instruments_signpost_pop();
  os_signpost_interval_end(instruments_log, signpost, "Trace");
}
#endif

#if PROFILE_SPALL
internal inline void
spall_begin(char *fmt, ...)
{
  if(spall_buffer.data == 0)
  {
    spall_buffer.length = MB(1);
    spall_buffer.data   = os_reserve(spall_buffer.length);
    os_commit(spall_buffer.data, spall_buffer.length);
    spall_buffer_init(&spall_profile, &spall_buffer);
  }
  if(spall_pid == 0)
  {
    spall_pid = os_get_process_info()->pid;
  }
  if(spall_tid == 0)
  {
    spall_tid = os_tid();
  }
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 string = push_str8fv(scratch.arena, fmt, args);
  spall_buffer_begin_ex(&spall_profile, &spall_buffer, string.str, string.size, os_now_microseconds(), spall_tid, spall_pid);
  va_end(args);
  scratch_end(scratch);
}
#endif
