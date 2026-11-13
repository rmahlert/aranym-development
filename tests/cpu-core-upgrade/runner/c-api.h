/* SPDX-License-Identifier: GPL-2.0-or-later
 * Experimental single-instance integration boundary. No UAE/C++ types escape.
 */
#ifndef MODERN040_EXPERIMENTAL_C_API_H
#define MODERN040_EXPERIMENTAL_C_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define M040_ABI_VERSION 1u
#define M040_OK 0
#define M040_BAD_ARGUMENT -1
#define M040_BUSY -2
#define M040_HOST_ERROR -3
enum m040_exit { M040_BUDGET, M040_STOPPED, M040_GUEST_EXCEPTION, M040_MEMORY_FAULT,
    M040_UNSUPPORTED, M040_UNSUPPORTED_MMU, M040_RESET, M040_DOUBLE_FAULT,
    M040_HOST_SERVICE, M040_HOST_RETURN };
enum m040_access_kind { M040_FETCH, M040_READ, M040_WRITE };
enum m040_service_kind { M040_NATFEAT_ID=1, M040_NATFEAT_CALL, M040_EMULOP, M040_EMULOP_RETURN, M040_LEGACY_FPU };
struct m040_options { uint32_t abi_version; uint32_t deliver_exceptions, lc040_probes, log_accesses; };
struct m040_state {
    uint32_t abi_version;
    uint32_t regs[16], pc, instruction_pc, usp, isp, msp;
    uint32_t vbr, sfc, dfc, cacr, caar, itt0, itt1, dtt0, dtt1;
    uint32_t tcr, mmusr, urp, srp, buscr, pcr;
    uint16_t sr;
    uint16_t stopped;
};
struct m040_bus_request {
    uint32_t abi_version, kind, address, size, function_code;
    uint32_t has_alternate_function_code, alternate_function_code, transfer_type;
};
/* Return1 only if completed,0 for a non-completed access. Data is numeric BE.
 * No callback may call state/config/run APIs; those reject reentry with BUSY.
 */
typedef int (*m040_bus_callback)(void *, const struct m040_bus_request *, uint32_t *);
enum m040_bus_completion { M040_BUS_UNKNOWN_FAULT=0, M040_BUS_COMPLETED=1, M040_BUS_FAULT_BEFORE_TRANSFER=2 };
/* BEFORE_TRANSFER certifies no part of THIS transfer reached the target.
 * Earlier accesses in the instruction may have completed. Never retry here. */
typedef int (*m040_precise_bus_callback)(void *, const struct m040_bus_request *, uint32_t *);
/* Additive opt-in contract; existing request/options/state layouts stay ABI1.
 * Return1 for a valid reply,0 for an unknown failure. The library initializes
 * this reply separately for every request. Metadata describes the current
 * failed request only; it never certifies CPU instruction completion.
 */
#define M040_BUS_DETAIL_VERSION 1u
enum m040_fault_origin { M040_ORIGIN_UNSPECIFIED=0, M040_ORIGIN_EXTERNAL_BUS=0x40,
    M040_ORIGIN_ATC_TRANSLATION=0x41 };
struct m040_bus_detail {
    uint32_t abi_version, completion, origin;
    uint32_t whole_scalar_no_commit, has_cpu_physical_address, cpu_physical_address;
};
/* whole_scalar_no_commit covers every byte of the original scalar request,
 * not one failed beat following a completed prefix. CPU physical address is
 * distinct from a backend's device-decode index. On COMPLETED, fault-only
 * metadata is ignored. Ambiguous/unknown evidence never implies a stage.
 */
typedef int (*m040_detailed_bus_callback)(void *, const struct m040_bus_request *, uint32_t *, struct m040_bus_detail *);
typedef int (*m040_reset_callback)(void *);
struct m040_result {
    uint32_t abi_version, exit, vector, instruction_pc, current_pc, address, size;
    uint64_t instructions;
    char detail[192];
};
struct m040_service {
    uint32_t abi_version, kind, opcode, pc, stack, supervisor;
};
struct m040_exception_event {
    uint64_t count;
    uint32_t abi_version, vector, format, stack, handler, stacked_pc, saved_sr, interrupt;
};
int m040_reset(const struct m040_options *);
int m040_get_state(struct m040_state *);
/* Requires canonical active-bank/A7 consistency. Retains nonarchitectural
 * helper state and any fault/capability latch; reset is required after faults.
 */
int m040_set_state(const struct m040_state *);
int m040_set_bus(m040_bus_callback, void *opaque);
int m040_set_precise_bus(m040_precise_bus_callback, void *opaque);
/* Selection alone retains terminal failures and does not enable WB1/WB2
 * or an MMU. Architectural delivery requires the separate opt-in below. */
int m040_set_detailed_bus(m040_detailed_bus_callback, void *opaque);
/* Separate opt-in: explicit ExternalBus, completed whole-scalar final writes
 * only. Unknown/ATC/partial/unfinished/traced/master cases remain terminal.
 * Detailed callback selection alone retains its terminal-failure contract. */
int m040_physical_write_exceptions(int enabled);
/* Genuine 040 restart-aware physical-bus bodies; no paged MMU, FPU/trace
 * continuation, MOVE16 or unknown partial-transfer recovery. */
int m040_fault_recovery(int enabled);
int m040_restart_pending(int *pending);
/* Limit diagnostic history without changing total counts or the last event.
 * Production frontends can use zero; tests default to full history. */
int m040_exception_history_limit(uint32_t entries);
/* Callback returns1 after platform hardware reset completed,0 on failure. */
int m040_set_reset_handler(m040_reset_callback, void *opaque);
int m040_poke(uint32_t address, uint32_t size, uint32_t value);
int m040_run(uint64_t budget, struct m040_result *);
int m040_interrupt(uint32_t vector, uint32_t level, int *accepted, struct m040_result *);
int m040_private_operations(int enabled);
int m040_get_service(struct m040_service *);
int m040_complete_service(struct m040_result *);
/* External synchronous host-service exception. Access/address faults require
 * richer metadata and are rejected here; they originate through the bus/core.
 */
int m040_exception(uint32_t vector, struct m040_result *);
/* Whitelisted trace-free pure-read probes; generic RTE7 stays blocked. */
int m040_read_probe_exceptions(int enabled);
int m040_get_exception_event(struct m040_exception_event *);
/* Explicit external engine selection; defaultoff. Suspends before extension
 * fetch, permits already-executed external advanced PC, and never replays it.
 * No FPU register file/engine is supplied by this C boundary. */
int m040_legacy_fpu_operations(int enabled);
#ifdef __cplusplus
}
#endif
#endif
