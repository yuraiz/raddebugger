// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Includes

#include "generated/mig_server.c"

////////////////////////////////
//~ Mach Exceptions

internal mach_msg_return_t
mach_msg_recv(
  MAC_DMN_MachMessage *msg,
  mach_port_t rcv_name,
  mach_msg_options_t options,
  mach_msg_timeout_t timeout)
{
  return mach_msg(
    &msg->hdr,
    options | MACH_RCV_MSG,
    0,
    sizeof(*msg),
    rcv_name,
    timeout,
    MACH_PORT_NULL
  );
}

internal mach_msg_return_t
mach_msg_send_reply(MAC_DMN_MachMessage *msg)
{
  return mach_msg(
    &msg->hdr,
    MACH_SEND_MSG | MACH_SEND_INTERRUPT,
    msg->hdr.msgh_size,
    0,
    MACH_PORT_NULL,
    MACH_MSG_TIMEOUT_NONE,
    MACH_PORT_NULL
  );
}

////////////////////////////////
//~ Mach Exceptions

internal mach_port_t
mac_dmn_make_exception_port() 
{
  kern_return_t status_code;

  task_t self = mach_task_self();
  
  mach_port_t exc_port = 0;
  status_code = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &exc_port);
  if(status_code != 0)
  {
    fprintf(stderr, "failed to allocate port: %s\n", mach_error_string(status_code));
    abort_self(1);
  }

  status_code = mach_port_insert_right(self, exc_port, exc_port, MACH_MSG_TYPE_MAKE_SEND);
  if(status_code != 0)
  {
    fprintf(stderr, "failed call to port insert right: %s\n", mach_error_string(status_code));
    abort_self(1);
  }

  return exc_port;
}

internal void
mac_dmn_subscribe_to_exceptions(task_t task, mach_port_t exc_port)
{
  kern_return_t status_code = task_set_exception_ports(
    task,
    EXC_MASK_ALL | EXC_MASK_CRASH | EXC_MASK_CORPSE_NOTIFY,
    exc_port,
    EXCEPTION_DEFAULT | MACH_EXCEPTION_MASK,
    THREAD_STATE_NONE
  );

  if(status_code != 0)
  {
    fprintf(stderr, "failed to set exception ports: %s\n", mach_error_string(status_code));
    abort_self(1);
  }
}

internal B32
mac_dmn_reply_to_pending_exceptions_for_task(task_t task)
{
  B32 should_resume = 0;

  pid_t target_pid;
  pid_for_task(task, &target_pid);
  
  for EachNode(exception, MAC_DMN_ExceptionResult, mac_dmn_exception_state->first_exception)
  {
    // total_exc++;
    if(exception->task == task)
    {
      //- yuraiz: handle UNIX soft signal
      if (exception->exception == EXC_SOFTWARE && exception->code == EXC_SOFT_SIGNAL) {
        ptrace(PT_THUPDATE,
                target_pid,
                (caddr_t)(uintptr_t)exception->thread,
                exception->subcode);
      }

      ptrace(PT_CONTINUE, target_pid, (caddr_t)1, 0);

      kern_return_t reply_kr = mach_msg_send_reply(&exception->reply);
      Assert(reply_kr == 0);

      DLLRemove(mac_dmn_exception_state->first_exception, mac_dmn_exception_state->last_exception, exception);
      SLLStackPush(mac_dmn_exception_state->free_exception, exception);

      should_resume = 1;
    }
  }

  return should_resume;
}

internal MAC_DMN_ExceptionResult
mac_dmn_wait_for_exception(mach_port_t exc_port)
{
  kern_return_t status_code = 0;

  {
    mach_msg_timeout_t timeout_ms = 17;

    MAC_DMN_MachMessage request = {};
    MAC_DMN_MachMessage reply = {};
    status_code = mach_msg_recv(&request, exc_port, MACH_RCV_INTERRUPT | MACH_RCV_TIMEOUT, timeout_ms);

    if (status_code == MACH_MSG_SUCCESS)
    {
      mach_exc_server(&request.hdr, &reply.hdr);
      mac_dmn_exception_state->last_exception->reply = reply;
    }

    while(1)
    {
      if (mach_msg_recv(&request, exc_port, MACH_RCV_INTERRUPT | MACH_RCV_TIMEOUT, 0) == MACH_MSG_SUCCESS)
      {
        Assert(0 && "bulk messages aren't handled yet");
        mach_exc_server(&request.hdr, &reply.hdr);
        mac_dmn_exception_state->last_exception->reply = reply;
      }
      else
      {
        break;
      }
    }
  }

  if(status_code == MACH_RCV_TIMED_OUT)
  {
    MAC_DMN_ExceptionResult result = {0};
    result.timed_out = true;
    return result;
  }
  if(status_code != 0)
  {
    fprintf(stderr, "mach_msg_server_once returned error: %x %s\n", status_code, mach_error_string(status_code));
  }

  MAC_DMN_ExceptionResult result = *mac_dmn_exception_state->last_exception;
  return result;
}

////////////////////////////////
//~ Mach exception handlers

internal const char*
exc_type_to_string(exception_type_t exception) {
  switch (exception) {
  case EXC_BAD_ACCESS:         return "EXC_BAD_ACCESS";
  case EXC_BAD_INSTRUCTION:    return "EXC_BAD_INSTRUCTION";
  case EXC_ARITHMETIC:         return "EXC_ARITHMETIC";
  case EXC_EMULATION:          return "EXC_EMULATION";
  case EXC_SOFTWARE:           return "EXC_SOFTWARE";
  case EXC_BREAKPOINT:         return "EXC_BREAKPOINT";
  case EXC_SYSCALL:            return "EXC_SYSCALL";
  case EXC_MACH_SYSCALL:       return "EXC_MACH_SYSCALL";
  case EXC_RPC_ALERT:          return "EXC_RPC_ALERT";
#ifdef EXC_CRASH
  case EXC_CRASH:              return "EXC_CRASH";
#endif
  case EXC_RESOURCE:           return "EXC_RESOURCE";
#ifdef EXC_GUARD
  case EXC_GUARD:              return "EXC_GUARD";
#endif
#ifdef EXC_CORPSE_NOTIFY
  case EXC_CORPSE_NOTIFY:      return "EXC_CORPSE_NOTIFY";
#endif
#ifdef EXC_CORPSE_VARIANT_BIT
  case EXC_CORPSE_VARIANT_BIT: return "EXC_CORPSE_VARIANT_BIT";
#endif
  }
  return "UNKNOWN";
}

extern kern_return_t
catch_mach_exception_raise(
  mach_port_t exception_port,
  mach_port_t thread,
  mach_port_t task,
  exception_type_t exception,
  mach_exception_data_t code,
  mach_msg_type_number_t code_count
)
{
  if(mac_dmn_exception_state->first_exception == 0)
  {
    task_suspend(task);
  }

  if(exception == EXC_BREAKPOINT && code[0] == 1 && code[1] == 0)
  {
    mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
    arm_debug_state64_t debug_state = {0};
    thread_get_state(thread, ARM_DEBUG_STATE64, (thread_state_t)&debug_state, &count);
    debug_state.__mdscr_el1 = 0;
    thread_set_state(thread, ARM_DEBUG_STATE64, (thread_state_t)&debug_state, ARM_DEBUG_STATE64_COUNT);
  }

  // MAC_DMN_ExceptionResult result = {0};
  MAC_DMN_ExceptionResult *result = mac_dmn_exception_state->free_exception;
  if(result)
  {
    SLLStackPop(mac_dmn_exception_state->free_exception);
  }
  else
  {
    result = push_array(mac_dmn_exception_state->arena, MAC_DMN_ExceptionResult, 1);
  }

  DLLPushBack(mac_dmn_exception_state->first_exception, mac_dmn_exception_state->last_exception, result);

  result->exception_port = exception_port;
  result->thread = thread;
  result->task = task;
  result->exception = exception;
  if(code_count > 0) { result->code = code[0]; }
  if(code_count > 1) { result->subcode = code[1]; }

  if (exception == EXC_BREAKPOINT)
  {
    // skip printing
  }
  else if (exception == EXC_SOFTWARE && code[0] == EXC_SOFT_SIGNAL)
  {
    printf("Got exception %s (code: EXC_SOFT_SIGNAL subcode: %s)\n",
      exc_type_to_string(result->exception), 
      strsignal(result->subcode)
    );
  }
  else
  {
    printf("Got exception %s (code: %llu, subcode: %p)\n",
      exc_type_to_string(result->exception), 
      result->code,
      result->subcode
    );
  }

  return KERN_SUCCESS;
}

// Unused handles

extern kern_return_t
catch_mach_exception_raise_state(
  mach_port_t exception_port,
  exception_type_t exception,
  mach_exception_data_t code,
  mach_msg_type_number_t code_count,
  int* flavor,
  thread_state_t in_state,
  mach_msg_type_number_t in_state_count,
  thread_state_t out_state,
  mach_msg_type_number_t* out_state_count
)
{
  Assert(0 && "this handler should not be called\n");
  return MACH_RCV_INVALID_TYPE;
}

extern kern_return_t
catch_mach_exception_raise_state_identity(
  mach_port_t exception_port,
  mach_port_t thread,
  mach_port_t task,
  exception_type_t exception,
  mach_exception_data_t code,
  mach_msg_type_number_t code_count,
  int* flavor,
  thread_state_t in_state,
  mach_msg_type_number_t in_state_count,
  thread_state_t out_state,
  mach_msg_type_number_t* out_state_count
)
{
  Assert(0 && "this handler should not be called\n");
  return MACH_RCV_INVALID_TYPE;
}
