// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Helpers

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
    EXC_MASK_ALL,
    exc_port,
    EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
    THREAD_STATE_NONE
  );

  if(status_code != 0)
  {
    fprintf(stderr, "failed to set exception ports: %s\n", mach_error_string(status_code));
    abort_self(1);
  }
}

internal MAC_DMN_ExceptionResult
mac_dmn_wait_for_exception(mach_port_t exc_port)
{
	DMN_MAC_EXC_Request request_msg = {};

	if(mac_dmn_exception_state->reply_pending)
	{
		mac_dmn_exception_state->reply_pending = 0;
		{
			// Assert it's suspended exactly once before resuming and replying to the exception
			task_t task = mac_dmn_exception_state->last_request.task.name;
			thread_t thread = mac_dmn_exception_state->last_request.thread.name;

			struct task_basic_info info;
			mach_msg_type_number_t info_cnt = TASK_BASIC_INFO_COUNT;

			// task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &info_cnt);
			// Assert(info.suspend_count == 1);


			// task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &info_cnt);

			// Assert(info.suspend_count == 0);

		// {
		// 	task_t task = mac_dmn_exception_state->last_request.task.name;
		// 	thread_t thread = mac_dmn_exception_state->last_request.thread.name;

		// 	pid_t target_pid;
		// 	pid_for_task(task, &target_pid);
		// 	ptrace(PT_CONTINUE, target_pid, (caddr_t)1, 0);

		// }


			// task_info(task, TASK_BASIC_INFO, (task_info_t)&info, &info_cnt);
			// Assert(info.suspend_count == 0);


			thread_state_flavor_t flavor = 0;
			mach_msg_type_number_t count = 0;
  		U32 state_buf[1296] = {};

			{
				flavor = ARM_THREAD_STATE64;
				count = ARM_THREAD_STATE64_COUNT;
				thread_get_state(thread, flavor, (thread_state_t)state_buf, &count);
				thread_set_state(thread, flavor, (thread_state_t)state_buf, count);
			}
			{
				flavor = ARM_DEBUG_STATE64;
				count = ARM_DEBUG_STATE64_COUNT;
				thread_get_state(thread, flavor, (thread_state_t)state_buf, &count);
				thread_set_state(thread, flavor, (thread_state_t)state_buf, count);
			}

			__sync_synchronize();  // Compiler barrier
			usleep(1);

			thread_suspend(thread);

			__sync_synchronize();  // Compiler barrier
			usleep(1);

		}

		B32 ok = mach_exc_reply_to(mac_dmn_exception_state->last_request);
		Assert(ok);

		{
			task_t task = mac_dmn_exception_state->last_request.task.name;
			thread_t thread = mac_dmn_exception_state->last_request.thread.name;
			task_resume(task);
			thread_resume(thread);

			__sync_synchronize();  // Compiler barrier
			usleep(1);

		}

	}

	kern_return_t status_code = mach_exc_recv(exc_port, &request_msg, 17);

	MAC_DMN_ExceptionResult result = {0};

	if(status_code == 0)
	{
		result.exception_port = request_msg.Head.msgh_local_port;
		result.thread = request_msg.thread.name;
		result.task = request_msg.task.name;
		result.exception = request_msg.exception;
		if(request_msg.codeCnt > 0) { result.code       = request_msg.code[0]; }
		if(request_msg.codeCnt > 1) { result.subcode    = request_msg.code[1]; }
		if(request_msg.codeCnt > 2) { result.subsubcode = request_msg.code[2]; }

		if (result.exception == EXC_BREAKPOINT)
		{
			// skip printing
		}
		else if(result.exception == EXC_SOFTWARE && result.code == EXC_SOFT_SIGNAL)
		{
			// handling UNIX soft signal
		
			printf("Got exception %s (code: EXC_SOFT_SIGNAL subcode: %s)\n",
				exc_type_to_string(result.exception), 
				strsignal(result.subcode)
			);


		}
		else
		{
			printf("Got exception %s (code: %llu, subcode: %p, subsubcode: %p)\n",
				exc_type_to_string(result.exception), 
				result.code,
				result.subcode,
				result.subsubcode
			);
		}

		task_suspend(result.task);
		// task_suspend(result.task);

		mac_dmn_exception_state->last_request = request_msg;
		mac_dmn_exception_state->reply_pending = 1;
	}
	else
	{
		if(status_code != MACH_RCV_TIMED_OUT)
		{
    	fprintf(stderr, "mach_exc_recv returned error: %x %s\n", status_code, mach_error_string(status_code));
		}

    result.timed_out = 1;
	}

  return result;
}

////////////////////////////////
//~ Mach exception handlers

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

internal mach_msg_return_t
mach_exc_recv(mach_port_t rcv_name, DMN_MAC_EXC_Request *request_msg, mach_msg_timeout_t timeout_ms)
{
	mach_msg_options_t options = 0;

	mach_msg_size_t request_size;
	mach_msg_return_t mr = 0;
	kern_return_t kr = 0;

	options &= ~(MACH_SEND_MSG | MACH_RCV_MSG | MACH_RCV_VOUCHER);
	if(timeout_ms > 0)
	{
		options |= MACH_RCV_TIMEOUT;
	}

	{
		U8 msg_buf[2056] = {};
		request_size = sizeof(msg_buf);
		mr = mach_msg((mach_msg_header_t *)msg_buf, options | MACH_RCV_MSG | MACH_RCV_INTERRUPT,
				0, request_size, rcv_name,
				timeout_ms, MACH_PORT_NULL);
		// write the output
		if(mr == MACH_MSG_SUCCESS)
		{
			MemoryCopyStruct(request_msg, msg_buf);
		}
	}

	return mr;
}

internal B32
mach_exc_reply_to(DMN_MAC_EXC_Request request_msg)
{
  mach_msg_timeout_t timeout = 17; // ms I guess

	mach_msg_return_t mr = 0;
	if (request_msg.Head.msgh_size != 0) {
		// voucher_mach_msg_state_t old_state  = voucher_mach_msg_adopt(&request_msg.Head);

		// compose the reply message
		mig_reply_error_t reply_msg = {};
		reply_msg.Head.msgh_size = sizeof(reply_msg);
		reply_msg.Head.msgh_local_port = MACH_PORT_NULL;
		reply_msg.Head.msgh_reserved = 0;

		reply_msg.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(request_msg.Head.msgh_bits), 0);
		reply_msg.Head.msgh_remote_port = request_msg.Head.msgh_remote_port;
		reply_msg.Head.msgh_id = request_msg.Head.msgh_id + 100;

		reply_msg.RetCode = KERN_SUCCESS;
		MACH_RCV_SUCCESS;
		reply_msg.NDR = NDR_record;

		if(request_msg.exception == EXC_SOFTWARE && request_msg.code[0] == EXC_SOFT_SIGNAL)
		{
			pid_t target_pid;
			pid_for_task(request_msg.task.name, &target_pid);
			ptrace(PT_THUPDATE,
							target_pid,
							(caddr_t)(uintptr_t)request_msg.thread.name,
							(S32)request_msg.code[1]);
		}

		if (reply_msg.Head.msgh_remote_port != MACH_PORT_NULL) {
			// mr = mach_msg(&reply_msg.Head,
			//     (MACH_MSGH_BITS_REMOTE(reply_msg.Head.msgh_bits) ==
			//     MACH_MSG_TYPE_MOVE_SEND_ONCE) ?
			//     MACH_SEND_MSG :
			//     MACH_SEND_MSG | MACH_SEND_TIMEOUT,
			//     reply_msg.Head.msgh_size, 0, MACH_PORT_NULL,
			//     timeout, MACH_PORT_NULL);

			mr = mach_msg(&reply_msg.Head,
							MACH_SEND_MSG|MACH_SEND_INTERRUPT,
							reply_msg.Head.msgh_size,
							0,
							MACH_PORT_NULL,
							MACH_MSG_TIMEOUT_NONE,
							MACH_PORT_NULL);

			if (mach_msg_server_is_recoverable_send_error(mr)) {
				mach_msg_server_consume_unsent_message(&reply_msg.Head);
				mr = MACH_MSG_SUCCESS;
			}
		}

		// voucher_mach_msg_revert(old_state);
	}

	return mr == MACH_MSG_SUCCESS;
}
