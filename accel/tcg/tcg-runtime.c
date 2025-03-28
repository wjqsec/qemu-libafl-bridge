/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "cpu.h"
#include "exec/helper-proto-common.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "exec/log.h"
#include "tcg/tcg.h"

#define HELPER_H  "accel/tcg/tcg-runtime.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

//// --- Begin LibAFL code ---

#include "libafl/exit.h"

#ifndef CONFIG_USER_ONLY

#include "sysemu/runstate.h"
#include "migration/snapshot.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/core/cpu.h"
#include "sysemu/hw_accel.h"
#include <stdlib.h>
#include <string.h>

void libafl_save_qemu_snapshot(char *name, bool sync);
void libafl_load_qemu_snapshot(char *name, bool sync);

static void save_snapshot_cb(void* opaque)
{
    char* name = (char*)opaque;
    Error *err = NULL;
    if(!save_snapshot(name, true, NULL, false, NULL, &err)) {
        error_report_err(err);
        error_report("Could not save snapshot");
    }
    free(opaque);
}

void libafl_save_qemu_snapshot(char *name, bool sync)
{
    // use snapshots synchronously, use if main loop is not running
    if (sync) {
        //TODO: eliminate this code duplication
        //by passing a heap-allocated buffer from rust to c,
        //which c needs to free
        Error *err = NULL;
        if(!save_snapshot(name, true, NULL, false, NULL, &err)) {
            error_report_err(err);
            error_report("Could not save snapshot");
        }
        return;
    }
    char* name_buffer = malloc(strlen(name)+1);
    strcpy(name_buffer, name);
    aio_bh_schedule_oneshot_full(qemu_get_aio_context(), save_snapshot_cb, (void*)name_buffer, "save_snapshot");
}

static void load_snapshot_cb(void* opaque)
{
    char* name = (char*)opaque;
    Error *err = NULL;

    int saved_vm_running = runstate_is_running();
    vm_stop(RUN_STATE_RESTORE_VM);

    bool loaded = load_snapshot(name, NULL, false, NULL, &err);

    if(!loaded) {
        error_report_err(err);
        error_report("Could not load snapshot");
    }
    if (loaded && saved_vm_running) {
        vm_start();
    }
    free(opaque);
}

void libafl_load_qemu_snapshot(char *name, bool sync)
{
    // use snapshots synchronously, use if main loop is not running
    if (sync) {
        //TODO: see libafl_save_qemu_snapshot
        Error *err = NULL;

        int saved_vm_running = runstate_is_running();
        vm_stop(RUN_STATE_RESTORE_VM);

        bool loaded = load_snapshot(name, NULL, false, NULL, &err);

        if(!loaded) {
            error_report_err(err);
            error_report("Could not load snapshot");
        }
        if (loaded && saved_vm_running) {
            vm_start();
        }
        return;
    }
    char* name_buffer = malloc(strlen(name)+1);
    strcpy(name_buffer, name);
    aio_bh_schedule_oneshot_full(qemu_get_aio_context(), load_snapshot_cb, (void*)name_buffer, "load_snapshot");
}

#endif

void HELPER(libafl_qemu_handle_breakpoint)(CPUArchState *env, uint64_t pc)
{
    CPUState* cpu = env_cpu(env);
    libafl_exit_request_breakpoint(cpu, (target_ulong) pc);
}

void HELPER(libafl_qemu_handle_sync_backdoor)(CPUArchState *env, uint64_t pc)
{
    CPUState* cpu = env_cpu(env);
    libafl_exit_request_sync_backdoor(cpu, (target_ulong) pc);
}


struct libafl_pre_memrw_hook {
    void (*callback)(uint64_t data, target_ulong pc, target_ulong addr, uint64_t size, target_ulong *out_addr, uint32_t rw, __uint128_t value);
    uint64_t data;
};
extern struct libafl_pre_memrw_hook* libafl_pre_memrw_hooks;
extern target_ulong libafl_gen_cur_pc;
uint64_t HELPER(libafl_qemu_pre_memrw)(uint64_t addr, uint64_t size, uint32_t rw, uint64_t low64_val, uint64_t high64_val)
{
    uint64_t out_addr = addr;
    if(libafl_pre_memrw_hooks)
    {
        __uint128_t value = ((__uint128_t)low64_val) |  (((__uint128_t)high64_val) << 64);
        libafl_pre_memrw_hooks->callback(libafl_pre_memrw_hooks->data,libafl_gen_cur_pc, addr, size, &out_addr, rw, value);
    }
    return out_addr;
}

//// --- End LibAFL code ---

/* 32-bit helpers */

int32_t HELPER(div_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 / arg2;
}

int32_t HELPER(rem_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 % arg2;
}

uint32_t HELPER(divu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 / arg2;
}

uint32_t HELPER(remu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 % arg2;
}

/* 64-bit helpers */

uint64_t HELPER(shl_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 << arg2;
}

uint64_t HELPER(shr_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(sar_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(div_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 / arg2;
}

int64_t HELPER(rem_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(divu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 / arg2;
}

uint64_t HELPER(remu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(muluh_i64)(uint64_t arg1, uint64_t arg2)
{
    uint64_t l, h;
    mulu64(&l, &h, arg1, arg2);
    return h;
}

int64_t HELPER(mulsh_i64)(int64_t arg1, int64_t arg2)
{
    uint64_t l, h;
    muls64(&l, &h, arg1, arg2);
    return h;
}

uint32_t HELPER(clz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? clz32(arg) : zero_val;
}

uint32_t HELPER(ctz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? ctz32(arg) : zero_val;
}

uint64_t HELPER(clz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? clz64(arg) : zero_val;
}

uint64_t HELPER(ctz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? ctz64(arg) : zero_val;
}

uint32_t HELPER(clrsb_i32)(uint32_t arg)
{
    return clrsb32(arg);
}

uint64_t HELPER(clrsb_i64)(uint64_t arg)
{
    return clrsb64(arg);
}

uint32_t HELPER(ctpop_i32)(uint32_t arg)
{
    return ctpop32(arg);
}

uint64_t HELPER(ctpop_i64)(uint64_t arg)
{
    return ctpop64(arg);
}

void HELPER(exit_atomic)(CPUArchState *env)
{
    cpu_loop_exit_atomic(env_cpu(env), GETPC());
}
