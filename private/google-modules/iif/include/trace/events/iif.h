/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trace events for IIF.
 *
 * Copyright (c) 2026 Google LLC
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iif

#if !defined(_TRACE_IIF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IIF_H

#include <linux/tracepoint.h>

#include <iif/iif-shared.h>
#include <iif/iif-strings.h>

#define IIF_TRACE_SYSTEM __stringify(TRACE_SYSTEM)

#define show_fence_type(type) __print_symbolic(type, IIF_FENCE_TYPE_SYMBOLS)
#define show_signaler_type(type) __print_symbolic(type, IIF_FENCE_SIGNALER_TYPE_SYMBOLS)
#define show_ip_type(type) __print_symbolic(type, IIF_IP_TYPE_SYMBOLS)

TRACE_EVENT(iif_created,

	    TP_PROTO(int fence_id, enum iif_fence_type fence_type,
		     enum iif_fence_signaler_type signaler_type, enum iif_ip_type signaler_ip,
		     u16 signalers),

	    TP_ARGS(fence_id, fence_type, signaler_type, signaler_ip, signalers),

	    TP_STRUCT__entry(__field(u32, fence_id) __field(u8, fence_type)
				     __field(u8, signaler_type) __field(u8, signaler_ip)
					     __field(u16, signalers)),

	    TP_fast_assign(__entry->fence_id = fence_id; __entry->fence_type = fence_type;
			   __entry->signaler_type = signaler_type;
			   __entry->signaler_ip = signaler_ip; __entry->signalers = signalers;),

	    TP_printk("fence_id=%u fence_type=%s signaler_type=%s signaler_ip=%s signalers=%u",
		      __entry->fence_id, show_fence_type(__entry->fence_type),
		      show_signaler_type(__entry->signaler_type),
		      show_ip_type(__entry->signaler_ip), __entry->signalers));

TRACE_EVENT(
	iif_retired,

	TP_PROTO(int fence_id, u16 unsubmitted_signalers, u16 outstanding_waiters, bool signaled,
		 u64 timeline, int error),

	TP_ARGS(fence_id, unsubmitted_signalers, outstanding_waiters, signaled, timeline, error),

	TP_STRUCT__entry(__field(u32, fence_id) __field(u16, unsubmitted_signalers)
				 __field(u16, outstanding_waiters) __field(u8, signaled)
					 __field(u64, timeline) __field(s16, error)),

	TP_fast_assign(__entry->fence_id = fence_id;
		       __entry->unsubmitted_signalers = unsubmitted_signalers;
		       __entry->outstanding_waiters = outstanding_waiters;
		       __entry->signaled = signaled; __entry->timeline = timeline;
		       __entry->error = error;),

	TP_printk(
		"fence_id=%u unsubmitted_signalers=%u outstanding_waiters=%u signaled=%u timeline=%llu error=%d",
		__entry->fence_id, __entry->unsubmitted_signalers, __entry->outstanding_waiters,
		__entry->signaled, __entry->timeline, __entry->error));

#endif /* _TRACE_IIF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
