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
#define M040_FPU_STATE_VERSION 1u
#define M040_OK 0
#define M040_BAD_ARGUMENT -1
#define M040_BUSY -2
#define M040_HOST_ERROR -3
enum m040_exit { M040_BUDGET, M040_STOPPED, M040_GUEST_EXCEPTION, M040_MEMORY_FAULT,
    M040_UNSUPPORTED, M040_UNSUPPORTED_MMU, M040_RESET, M040_DOUBLE_FAULT,
    M040_HOST_SERVICE, M040_HOST_RETURN };
enum m040_access_kind { M040_FETCH, M040_READ, M040_WRITE };
enum m040_service_kind { M040_NATFEAT_ID=1, M040_NATFEAT_CALL, M040_EMULOP, M040_EMULOP_RETURN, M040_LEGACY_FPU };
enum m040_fpu_binary_operation { M040_FPU_ADD, M040_FPU_SUB, M040_FPU_MUL, M040_FPU_DIV };
struct m040_options { uint32_t abi_version; uint32_t deliver_exceptions, lc040_probes, log_accesses; };
struct m040_state {
    uint32_t abi_version;
    uint32_t regs[16], pc, instruction_pc, usp, isp, msp;
    uint32_t vbr, sfc, dfc, cacr, caar, itt0, itt1, dtt0, dtt1;
    uint32_t tcr, mmusr, urp, srp, buscr, pcr;
    uint16_t sr;
    uint16_t stopped;
};
/* Canonical 68881/68882/68040 external 96-bit register image.  Keeping this
 * additive leaves the established integer ABI layout unchanged while giving
 * the native FPU frontend an exact, host-format-independent interchange.
 * words[][0] contains sign/exponent in bits31..16; words[][1..2] contain the
 * 64-bit significand, all in architectural big-endian word order.
 */
struct m040_fpu_state {
    uint32_t abi_version;
    uint32_t fp[8][3];
    uint32_t fpcr, fpsr, fpiar;
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
int m040_get_fpu_state(struct m040_fpu_state *);
/* Requires canonical active-bank/A7 consistency. Retains nonarchitectural
 * helper state and any fault/capability latch; reset is required after faults.
 */
int m040_set_state(const struct m040_state *);
int m040_set_fpu_state(const struct m040_fpu_state *);
/* Exact pinned SoftFloat backend operation on canonical FP registers.  This
 * does not fetch/decode an instruction or touch integer state; the instruction
 * frontend supplies operand and exception ordering separately.
 */
int m040_fpu_binary(uint32_t operation,uint32_t destination,uint32_t source);
int m040_fpu_sqrt(uint32_t destination,uint32_t source);
int m040_set_bus(m040_bus_callback, void *opaque);
int m040_set_precise_bus(m040_precise_bus_callback, void *opaque);
/* Selection alone retains terminal failures and does not enable WB1/WB2
 * or an MMU. Architectural delivery requires the separate opt-in below. */
int m040_set_detailed_bus(m040_detailed_bus_callback, void *opaque);
/* Separate opt-in: explicit ExternalBus, completed whole-scalar final writes
 * only. Unknown/ATC/partial/unfinished/traced/master cases remain terminal.
 * Detailed callback selection alone retains its terminal-failure contract. */
int m040_physical_write_exceptions(int enabled);
/* Independent opt-in for certified ExternalBus scalar reads/fetches. The
 * whole-scalar certificate covers no transfer of any byte of that original
 * request, including target read side effects; an explicit CPU physical
 * address is required. ATC/partial/unknown/traced/master remain terminal. */
int m040_physical_read_exceptions(int enabled);
/* Separate bounded profile for the pinned bitfield memory writers. Requires
 * the physical write profile and its explicit whole-scalar ExternalBus
 * certificate. Only a proved write-only CPU tail and one unissued IU write
 * can finish; this does not enable a generic queue or unknown/partial retry. */
int m040_bitfield_write_exceptions(int enabled);
/* Pure storage identity for the separate bounded completed-frame IRQ profile.
 * Return UNKNOWN unless the entire requested range has a stable identity.
 * RAM aliases MUST return the same contiguous opaque uint64 key; DISJOINT
 * certifies storage cannot overlap a RAM frame. This callback may not read a
 * target, access MMIO, change state or perform translation/table-walk effects.
 * Requests include the 60-byte frame range. Keys are backend storage offsets,
 * not architectural CPU physical addresses or guessed normalized addresses.
 */
enum m040_frame_storage_class { M040_FRAME_UNKNOWN=0, M040_FRAME_RAM=1, M040_FRAME_DISJOINT=2 };
typedef int (*m040_frame_storage_callback)(void*,const struct m040_bus_request*,uint64_t*);
int m040_set_frame_storage(m040_frame_storage_callback,void*);
/* Default-off; requires the pure storage callback. At most32 live identities;
 * exhaustion drops all identities and retains conservative holdoff until the
 * profile is reset. Only genuine successful RTE consumes an identity.
 * SavedSR-only edits are allowed; PC/format/EA/SSW writes invalidate. Copied,
 * relocated, untracked and ambiguous frames retain the old restart holdoff.
 * Replacing a bus callback clears the profile and storage callback contract.
 * ALL non-core RAM writers must notify before writing, or invalidate through
 * a barrier. Native/service/DMA cooperation is a backend obligation.
 */
int m040_completed_frame_irq(int enabled);
int m040_frame_write_notify(const struct m040_bus_request*);
/* Metadata-only same-CPU-thread barrier is also allowed within an existing
 * bus callback, before hardware DMA/copy writes; it performs no resolver or
 * target operation and does not make the core safe for concurrent threads. */
int m040_frame_barrier(void);
/* True only when the last core instruction consumed a qualified frame.
 * Native dispatch must leave its compiled chain and acknowledge pending IRQ
 * before its successor. The raw native restart holdoff remains set. */
int m040_completed_rte_event(int*);
/* Genuine 040 restart-aware physical-bus bodies; no paged MMU, FPU/trace
 * continuation, MOVE16 or unknown partial-transfer recovery. */
int m040_fault_recovery(int enabled);
int m040_restart_pending(int *pending);
/* Raw restart holdoff for native dispatch/compiler admission. Architectural
 * IRQ eligibility from m040_restart_pending must not replace this guard. */
int m040_native_restart_pending(int *pending);
/* Limit diagnostic history without changing total counts or the last event.
 * Production frontends can use zero; tests default to full history. */
int m040_exception_history_limit(uint32_t entries);
/* Callback returns1 after platform hardware reset completed,0 on failure. */
int m040_set_reset_handler(m040_reset_callback, void *opaque);
int m040_poke(uint32_t address, uint32_t size, uint32_t value);
int m040_run(uint64_t budget, struct m040_result *);
int m040_interrupt(uint32_t vector, uint32_t level, int *accepted, struct m040_result *);
/* Pure acceptance query before a platform acknowledges its source. No target
 * reads, vector acknowledgement, frame delivery or service completion. */
int m040_interrupt_ready(uint32_t level, int *ready);
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
/* Native pinned 68040 FPU instruction frontend. Mutually exclusive with the
 * external legacy service engine and default-off until explicitly selected. */
int m040_modern_fpu_operations(int enabled);
#ifdef __cplusplus
}
#endif
#endif
