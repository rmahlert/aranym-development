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
enum m040_service_kind { M040_NATFEAT_ID=1, M040_NATFEAT_CALL, M040_EMULOP, M040_EMULOP_RETURN };
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
/* Narrow trace-free TST.B(An) failed-read delivery; generic RTE7 stays blocked. */
int m040_read_probe_exceptions(int enabled);
int m040_get_exception_event(struct m040_exception_event *);
#ifdef __cplusplus
}
#endif
#endif
