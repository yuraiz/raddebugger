// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DEMON_OS_MAC_H
#define DEMON_OS_MAC_H

////////////////////////////////
//~ Includes

#include "generated/mig_server.h"
#include "generated/mig_client.h"

////////////////////////////////
//~ Exceptions

typedef struct DMN_MAC_MachMessage {
  union {
    mach_msg_header_t hdr;
    char data[2080];
  };
} DMN_MAC_MachMessage;

typedef struct DMN_MAC_ExceptionResult
{
  mach_port_t exception_port;
  mach_port_t thread;
  mach_port_t task;
  S32 exception;
  S64 code;
  S64 subcode;
  B32 timed_out;
} DMN_MAC_ExceptionResult;

////////////////////////////////
//~ Global State

typedef struct DMN_MAC_ExceptionState
{
  Arena *arena;
  DMN_MAC_ExceptionResult last_result;
} DMN_MAC_ExceptionState;

////////////////////////////////
//~ rjf: Globals

global DMN_MAC_ExceptionState *dmn_mac_exception_state = 0;

////////////////////////////////
//~ Mach Exceptions

internal mach_port_t dmn_mac_make_exception_port();
internal void dmn_mac_subscribe_to_exceptions(task_t task, mach_port_t exc_port);
internal DMN_MAC_ExceptionResult dmn_mac_wait_for_exception(mach_port_t exc_port);

////////////////////////////////
//~ Mach exception handlers

extern kern_return_t catch_mach_exception_raise(
  mach_port_t exception_port,
  mach_port_t thread,
  mach_port_t task,
  exception_type_t exception,
  mach_exception_data_t code,
  mach_msg_type_number_t code_count
);

extern kern_return_t catch_mach_exception_raise_state(
  mach_port_t exception_port,
  exception_type_t exception,
  mach_exception_data_t code,
  mach_msg_type_number_t code_count,
  int* flavor,
  thread_state_t in_state,
  mach_msg_type_number_t in_state_count,
  thread_state_t out_state,
  mach_msg_type_number_t* out_state_count
);

extern kern_return_t catch_mach_exception_raise_state_identity(
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
);

mach_msg_return_t
mach_msg_server_once_with_timeout(
	boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
	mach_msg_size_t max_size,
	mach_port_t rcv_name,
	mach_msg_options_t options
);

#endif // DEMON_OS_MAC_H