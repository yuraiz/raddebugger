// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DEMON_OS_MAC_H
#define DEMON_OS_MAC_H

////////////////////////////////
//~ Includes

////////////////////////////////
//~ Exceptions

typedef struct MAC_DMN_MachMessage {
  union {
    mach_msg_header_t hdr;
    char data[2080];
  };
} MAC_DMN_MachMessage;

#pragma pack(push, 4)
typedef struct DMN_MAC_EXC_Request
{
	mach_msg_header_t Head;
	/* start of the kernel processed data */
	mach_msg_body_t msgh_body;
	mach_msg_port_descriptor_t thread;
	mach_msg_port_descriptor_t task;
	/* end of the kernel processed data */
	NDR_record_t NDR;
	exception_type_t exception;
	mach_msg_type_number_t codeCnt;
	int64_t code[4];
} DMN_MAC_EXC_Request;
#pragma pack(pop)

#pragma pack(push, 4)
typedef struct DMN_MAC_EXC_RequestStateIdentity
{
  mach_msg_header_t Head;
  /* start of the kernel processed data */
  mach_msg_body_t msgh_body;
  mach_msg_port_descriptor_t thread;
  mach_msg_port_descriptor_t task;
  /* end of the kernel processed data */
  NDR_record_t NDR;
  exception_type_t exception;
  mach_msg_type_number_t codeCnt;
  int64_t code[2];
  int flavor;
  mach_msg_type_number_t old_stateCnt;
  natural_t old_state[1296];
} DMN_MAC_EXC_RequestStateIdentity;
#pragma pack(pop)

#pragma pack(push, 4)
typedef struct DMN_MAC_EXC_ReplyStateIdentity
{
  mach_msg_header_t Head;
  NDR_record_t NDR;
  kern_return_t RetCode;
  int flavor;
  mach_msg_type_number_t new_stateCnt;
  natural_t new_state[1296];
} DMN_MAC_EXC_ReplyStateIdentity;
#pragma pack(pop)

typedef struct MAC_DMN_ExceptionResult
{
  mach_port_t exception_port;
  mach_port_t thread;
  mach_port_t task;
  S32 exception;
  S64 code;
  S64 subcode;
  S64 subsubcode;
  B32 timed_out;
} MAC_DMN_ExceptionResult;

////////////////////////////////
//~ Global State

typedef struct MAC_DMN_ExceptionState
{
  Arena *arena;
  MAC_DMN_ExceptionResult last_result;
  DMN_MAC_EXC_Request last_request;
  B32 reply_pending;
} MAC_DMN_ExceptionState;

////////////////////////////////
//~ rjf: Globals

global MAC_DMN_ExceptionState *mac_dmn_exception_state = 0;

////////////////////////////////
//~ Mach Exceptions

internal mach_port_t mac_dmn_make_exception_port();
internal void mac_dmn_subscribe_to_exceptions(task_t task, mach_port_t exc_port);
internal MAC_DMN_ExceptionResult mac_dmn_wait_for_exception(mach_port_t exc_port);

internal mach_msg_return_t mach_exc_recv(mach_port_t rcv_name, DMN_MAC_EXC_Request *request_out, mach_msg_timeout_t timeout_ms);
internal B32 mach_exc_reply_to(DMN_MAC_EXC_Request request_msg);

mach_msg_return_t
mach_exc_server_once_with_timeout(
	mach_msg_size_t max_size,
	mach_port_t rcv_name,
	mach_msg_options_t options
);

#endif // DEMON_OS_MAC_H