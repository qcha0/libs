/*
Copyright (C) 2022 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <libpman.h>

#include "scap_modern_bpf.h"
#include "scap.h"
#include "scap-int.h"
#include "scap_procs.h"
#include "noop.h"
#include "../common/strlcpy.h"

/* Right now this is not used */
bool scap_modern_bpf__match(scap_open_args* oargs)
{
	return strncmp(oargs->engine_name, MODERN_BPF_ENGINE, MODERN_BPF_ENGINE_LEN) == 0;
}

static struct modern_bpf_engine* scap_modern_bpf__alloc_engine(scap_t* main_handle, char* lasterr_ptr)
{
	struct modern_bpf_engine* engine = calloc(1, sizeof(struct modern_bpf_engine));
	if(engine)
	{
		engine->m_lasterr = lasterr_ptr;
	}
	return engine;
}

static void scap_modern_bpf__free_engine(struct scap_engine_handle engine)
{
	free(engine.m_handle);
}

static int32_t scap_modern_bpf__next(struct scap_engine_handle engine, OUT scap_evt** pevent, OUT uint16_t* pcpuid)
{
	/// TODO: we need to extract the events in order like in the old probe.
	if(pman_consume_one_from_buffers((void**)pevent, pcpuid))
	{
		return SCAP_TIMEOUT;
	}
	return SCAP_SUCCESS;
}

static int32_t scap_modern_bpf__configure(struct scap_engine_handle engine, enum scap_setting setting, unsigned long arg1, unsigned long arg2)
{
	switch(setting)
	{
	case SCAP_SAMPLING_RATIO:
		/* Not supported */
		return SCAP_SUCCESS;
	case SCAP_TRACERS_CAPTURE:
		/* Not supported */
		return SCAP_SUCCESS;
	case SCAP_PAGE_FAULTS:
		/* Not supported */
		return SCAP_SUCCESS;
	case SCAP_SNAPLEN:
		pman_set_snaplen(arg1);
	case SCAP_EVENTMASK:
		/* Not supported */
		return SCAP_SUCCESS;
	case SCAP_DYNAMIC_SNAPLEN:
		/* Not supported */
		return SCAP_SUCCESS;
	case SCAP_SIMPLEDRIVER_MODE:
		/* Not supported */
		return SCAP_SUCCESS;
	case SCAP_FULLCAPTURE_PORT_RANGE:
		/* Not supported */
		return SCAP_SUCCESS;
	case SCAP_STATSD_PORT:
		/* Not supported */
		return SCAP_SUCCESS;
	default:
	{
		char msg[SCAP_LASTERR_SIZE];
		snprintf(msg, sizeof(msg), "Unsupported setting %d (args %lu, %lu)", setting, arg1, arg2);
		struct modern_bpf_engine* handle = engine.m_handle;
		strlcpy(handle->m_lasterr, msg, SCAP_LASTERR_SIZE);
		return SCAP_FAILURE;
	}
	}

	return SCAP_SUCCESS;
}

int32_t scap_modern_bpf__start_capture(struct scap_engine_handle engine)
{
	pman_enable_capture();
	return SCAP_SUCCESS;
}

int32_t scap_modern_bpf__stop_capture(struct scap_engine_handle engine)
{
	pman_disable_capture();
	return SCAP_SUCCESS;
}

int32_t scap_modern_bpf__init(scap_t* handle, scap_open_args* oargs)
{
	int ret = 0;
	struct scap_engine_handle engine = handle->m_engine;
	bool libbpf_verbosity = false;

	/* Configure libbpf library used under the hood. */
	if(pman_set_libbpf_configuration(libbpf_verbosity))
	{
		snprintf(handle->m_engine.m_handle->m_lasterr, SCAP_LASTERR_SIZE, "Unable to get configure libbpf.");
		return SCAP_FAILURE;
	}

	/* Return the number of system available CPUs, not online CPUs. */
	engine.m_handle->m_num_cpus = pman_get_cpus_number();

	/* Load and attach */
	ret = pman_open_probe();
	ret = ret ?: pman_prepare_ringbuf_array_before_loading();
	ret = ret ?: pman_prepare_maps_before_loading();
	ret = ret ?: pman_load_probe();
	ret = ret ?: pman_finalize_maps_after_loading();
	ret = ret ?: pman_finalize_ringbuf_array_after_loading();
	ret = ret ?: pman_attach_syscall_enter_dispatcher();
	ret = ret ?: pman_attach_syscall_exit_dispatcher();
	if(ret != SCAP_SUCCESS)
	{
		return ret;
	}

	handle->m_api_version = pman_get_probe_api_ver();
	handle->m_schema_version = pman_get_probe_schema_ver();

	/// TODO: Here we miss the simple consumer logic. Right now
	/// all syscalls are interesting.

	return SCAP_SUCCESS;
}

int32_t scap_modern_bpf__close(struct scap_engine_handle engine)
{
	pman_detach_all_programs();
	pman_close_probe();
	return SCAP_SUCCESS;
}

static uint32_t scap_modern_bpf__get_n_devs(struct scap_engine_handle engine)
{
	return engine.m_handle->m_num_cpus;
}

int32_t scap_modern_bpf__get_stats(struct scap_engine_handle engine, OUT scap_stats* stats)
{
	if(pman_get_scap_stats((void*)stats))
	{
		return SCAP_FAILURE;
	}
	return SCAP_SUCCESS;
}

int32_t scap_modern_bpf__get_n_tracepoint_hit(struct scap_engine_handle engine, OUT long* ret)
{
	if(pman_get_n_tracepoint_hit(ret))
	{
		return SCAP_FAILURE;
	}
	return SCAP_SUCCESS;
}

struct scap_vtable scap_modern_bpf_engine = {
	.name = "modern_bpf",
	.mode = SCAP_MODE_LIVE,
	.savefile_ops = NULL,

	.match = scap_modern_bpf__match,
	.alloc_handle = scap_modern_bpf__alloc_engine,
	.init = scap_modern_bpf__init,
	.free_handle = scap_modern_bpf__free_engine,
	.close = scap_modern_bpf__close,
	.next = scap_modern_bpf__next,
	.start_capture = scap_modern_bpf__start_capture,
	.stop_capture = scap_modern_bpf__stop_capture,
	.configure = scap_modern_bpf__configure,
	.get_stats = scap_modern_bpf__get_stats,
	.get_n_tracepoint_hit = scap_modern_bpf__get_n_tracepoint_hit,
	.get_n_devs = scap_modern_bpf__get_n_devs,
	.get_max_buf_used = noop_get_max_buf_used,
	.get_threadlist = scap_procfs_get_threadlist,
	.get_vpid = noop_get_vxid,
	.get_vtid = noop_get_vxid,
	.getpid_global = scap_os_getpid_global,
};