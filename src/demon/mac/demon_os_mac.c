// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Includes

#include "generated/mig_server.c"
#undef msgh_request_port
#undef msgh_reply_port
#include "generated/mig_client.c"

////////////////////////////////
//~ Mach Exceptions

internal mach_port_t
dmn_mac_make_exception_port() 
{
  kern_return_t status_code;

  task_t self = mach_task_self();
  
  mach_port_t exc_port = 0;
  status_code = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &exc_port);
  if(status_code != 0)
  {
    fprintf(stderr, "failed to allocate port: %s\n", mach_error_string(status_code));
    os_abort(1);
  }

  status_code = mach_port_insert_right(self, exc_port, exc_port, MACH_MSG_TYPE_MAKE_SEND);
  if(status_code != 0)
  {
    fprintf(stderr, "failed call to port insert right: %s\n", mach_error_string(status_code));
    os_abort(1);
  }

  return exc_port;
}

internal void
dmn_mac_subscribe_to_exceptions(task_t task, mach_port_t exc_port) 
{
  kern_return_t status_code = task_set_exception_ports(
    task,
    EXC_MASK_ALL,
    exc_port,
    EXCEPTION_DEFAULT | MACH_EXCEPTION_MASK,
    MACHINE_THREAD_STATE
  );

  if(status_code != 0)
  {
    fprintf(stderr, "failed to set exception ports: %s\n", mach_error_string(status_code));
    os_abort(1);
  }
}

internal DMN_MAC_ExceptionResult
dmn_mac_wait_for_exception(mach_port_t exc_port)
{
  const mach_msg_size_t msg_size = 2048;
  kern_return_t status_code = mach_msg_server_once_with_timeout(
    mach_exc_server, msg_size, exc_port, 0
  );

  if(status_code == MACH_RCV_TIMED_OUT)
  {
    DMN_MAC_ExceptionResult result = {0};
    result.timed_out = true;
    return result;
  }
  if(status_code != 0)
  {
    fprintf(stderr, "mach_msg_server_once returned error: %x %s\n", status_code, mach_error_string(status_code));
  }

  DMN_MAC_ExceptionResult result = dmn_mac_exception_state->last_result;
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
  DMN_MAC_ExceptionResult result = {0};
  result.exception_port = exception_port;
  result.thread = thread;
  result.task = task;
  result.exception = exception;
  result.code_count = code_count;
  for EachIndex(i, code_count)
  {
    result.exception_codes[i] = code[i];
  }

  printf("Got exception %s\n", exc_type_to_string(exception));

	if (result.exception == EXC_SOFTWARE && code[0] == EXC_SOFT_SIGNAL) {
		// handling UNIX soft signal
	
		pid_t target_pid;
		pid_for_task(task, &target_pid);
		ptrace(PT_THUPDATE,
						target_pid,
						(caddr_t)(uintptr_t)thread,
						code[2]);
	}

  task_suspend(task);

  dmn_mac_exception_state->last_result = result;

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
  fprintf(stderr, "this handler should not be called\n");
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
  fprintf(stderr, "this handler should not be called\n");
  return MACH_RCV_INVALID_TYPE;
}

// Edited message server

static inline boolean_t
mach_msg_server_is_recoverable_send_error(kern_return_t kr)
{
	switch (kr) {
	case MACH_SEND_INVALID_DEST:
	case MACH_SEND_TIMED_OUT:
	case MACH_SEND_INTERRUPTED:
		return TRUE;
	default:
		/*
		 * Other errors mean that the message may have been partially destroyed
		 * by the kernel, and these can't be recovered and may leak resources.
		 */
		return FALSE;
	}
}

static kern_return_t
mach_msg_server_mig_return_code(mig_reply_error_t *reply)
{
	/*
	 * If the message is complex, it is assumed that the reply was successful,
	 * as the RetCode is where the count of out of line descriptors is.
	 *
	 * If not, we read RetCode.
	 */
	if (reply->Head.msgh_bits & MACH_MSGH_BITS_COMPLEX) {
		return KERN_SUCCESS;
	}
	return reply->RetCode;
}

static void
mach_msg_server_consume_unsent_message(mach_msg_header_t *hdr)
{
	/* mach_msg_destroy doesn't handle the local port */
	mach_port_t port = hdr->msgh_local_port;
	if (MACH_PORT_VALID(port)) {
		switch (MACH_MSGH_BITS_LOCAL(hdr->msgh_bits)) {
		case MACH_MSG_TYPE_MOVE_SEND:
		case MACH_MSG_TYPE_MOVE_SEND_ONCE:
			/* destroy the send/send-once right */
			(void) mach_port_deallocate(mach_task_self_, port);
			hdr->msgh_local_port = MACH_PORT_NULL;
			break;
		}
	}
	mach_msg_destroy(hdr);
}

mach_msg_return_t
mach_msg_server_once_with_timeout(
	boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
	mach_msg_size_t max_size,
	mach_port_t rcv_name,
	mach_msg_options_t options)
{
  mach_msg_timeout_t timeout = 17; // ms I guess

	mig_reply_error_t *bufRequest, *bufReply;
	mach_msg_size_t request_size;
	mach_msg_size_t request_alloc;
	mach_msg_size_t trailer_alloc;
	mach_msg_size_t reply_alloc;
	mach_msg_return_t mr;
	kern_return_t kr;
	mach_port_t self = mach_task_self_;
	voucher_mach_msg_state_t old_state = VOUCHER_MACH_MSG_STATE_UNCHANGED;

	options &= ~(MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_VOUCHER);

	trailer_alloc = REQUESTED_TRAILER_SIZE(options);
	request_alloc = (mach_msg_size_t)round_page(max_size + trailer_alloc);


	request_size = (options & MACH_RCV_LARGE) ?
	    request_alloc : max_size + trailer_alloc;

	reply_alloc = (mach_msg_size_t)round_page((options & MACH_SEND_TRAILER) ?
	    (max_size + MAX_TRAILER_SIZE) :
	    max_size);

  // printf("request alloc: %d, rep: %d\n", request_alloc, reply_alloc);

	kr = vm_allocate(self,
	    (vm_address_t *)&bufReply,
	    reply_alloc,
	    VM_MAKE_TAG(VM_MEMORY_MACH_MSG) | TRUE);
	if (kr != KERN_SUCCESS) {
		return kr;
	}

	for (;;) {
		mach_msg_size_t new_request_alloc;

		kr = vm_allocate(self,
		    (vm_address_t *)&bufRequest,
		    request_alloc,
		    VM_MAKE_TAG(VM_MEMORY_MACH_MSG) | TRUE);
		if (kr != KERN_SUCCESS) {
			vm_deallocate(self,
			    (vm_address_t)bufReply,
			    reply_alloc);
			return kr;
		}

		mr = mach_msg(&bufRequest->Head, MACH_RCV_TIMEOUT | MACH_RCV_MSG | MACH_RCV_VOUCHER,
		    0, request_size, rcv_name,
		    timeout, MACH_PORT_NULL);

		if (!((mr == MACH_RCV_TOO_LARGE) && (options & MACH_RCV_LARGE))) {
			break;
		}

		new_request_alloc = (mach_msg_size_t)round_page(bufRequest->Head.msgh_size +
		    trailer_alloc);
		vm_deallocate(self,
		    (vm_address_t) bufRequest,
		    request_alloc);
		request_size = request_alloc = new_request_alloc;
	}

	if (mr == MACH_MSG_SUCCESS) {
		/* we have a request message */

		old_state = voucher_mach_msg_adopt(&bufRequest->Head);

		(void) (*demux)(&bufRequest->Head, &bufReply->Head);

		switch (mach_msg_server_mig_return_code(bufReply)) {
		case KERN_SUCCESS:
			break;
		case MIG_NO_REPLY:
			bufReply->Head.msgh_remote_port = MACH_PORT_NULL;
			break;
		default:
			/*
			 * destroy the request - but not the reply port
			 * (MIG moved it into the bufReply).
			 */
			bufRequest->Head.msgh_remote_port = MACH_PORT_NULL;
			mach_msg_destroy(&bufRequest->Head);
		}

		/*
		 *	We don't want to block indefinitely because the client
		 *	isn't receiving messages from the reply port.
		 *	If we have a send-once right for the reply port, then
		 *	this isn't a concern because the send won't block.
		 *	If we have a send right, we need to use MACH_SEND_TIMEOUT.
		 *	To avoid falling off the kernel's fast RPC path unnecessarily,
		 *	we only supply MACH_SEND_TIMEOUT when absolutely necessary.
		 */
		if (bufReply->Head.msgh_remote_port != MACH_PORT_NULL) {
			mr = mach_msg(&bufReply->Head,
			    (MACH_MSGH_BITS_REMOTE(bufReply->Head.msgh_bits) ==
			    MACH_MSG_TYPE_MOVE_SEND_ONCE) ?
			    MACH_SEND_MSG | options :
			    MACH_SEND_MSG | MACH_SEND_TIMEOUT | options,
			    bufReply->Head.msgh_size, 0, MACH_PORT_NULL,
			    timeout, MACH_PORT_NULL);

			if (mach_msg_server_is_recoverable_send_error(mr)) {
				mach_msg_server_consume_unsent_message(&bufReply->Head);
				mr = MACH_MSG_SUCCESS;
			}
		}
	}

	voucher_mach_msg_revert(old_state);

	(void)vm_deallocate(self,
	    (vm_address_t) bufRequest,
	    request_alloc);
	(void)vm_deallocate(self,
	    (vm_address_t) bufReply,
	    reply_alloc);
	return mr;
}