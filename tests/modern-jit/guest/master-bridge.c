/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ordinary guest regression for real ARAnyM NatFeat and trap/RTE integration.
 * Run only on a disposable profile. No disk or host networking is required.
 */
#include <stdint.h>
#include <stdio.h>
#include <mint/osbind.h>

extern long nf_id(const char *);
extern long nf_call(long, ...);
extern long nf_probe_id(const char *);
extern long nf_probe_call(long);
extern void trap_probe(void);
extern void trap_user_probe(void);
extern void trap_master_probe(void);
extern void privilege_probe(void);
extern void illegal_probe(void);
extern void bridge_trap_handler(void);

volatile uint32_t bridge_regs[16], bridge_before_sp;
volatile uint16_t bridge_sr;
volatile uint32_t bridge_trap_pc, bridge_expected_pc, bridge_frame_sp;
volatile uint16_t bridge_trap_sr, bridge_trap_format;
volatile uint16_t bridge_trap_mode, bridge_saved_sr;
volatile uint32_t bridge_super_sp, bridge_saved_bank, bridge_bank_top;
volatile uint32_t bridge_after_bank, bridge_resume_pc, bridge_exit_id;
uint32_t bridge_user_stack[64], bridge_master_stack[64];

#ifndef CPU_BRIDGE_EXPECT_PC_DELTA
#define CPU_BRIDGE_EXPECT_PC_DELTA 0
#endif

static const uint32_t sentinels[14] = {
    0x11111111UL, 0x22222222UL, 0x33333333UL, 0x44444444UL,
    0x55555555UL, 0x66666666UL, 0x77777777UL,
    0x01234560UL, 0x11234561UL, 0x21234562UL, 0x31234563UL,
    0x41234564UL, 0x51234565UL, 0x61234566UL
};

static int preserved_sp(uint32_t expected_sp)
{
    int i;
    for (i = 0; i < 14; ++i)
        if (bridge_regs[i + 1] != sentinels[i]) return 0;
    return bridge_regs[15] == expected_sp && (bridge_sr & 31) == 21;
}

static int preserved(void) { return preserved_sp(bridge_before_sp); }

static int frame(uint16_t saved_sr, uint16_t vector)
{
    return bridge_trap_pc == bridge_expected_pc &&
           bridge_trap_sr == saved_sr && bridge_trap_format == vector * 4;
}

static long checks(void)
{
    long version, name, stderr_id, first_error = 0;
    void (*old_vector)(void);
    char host_name[16] = {0};

    stderr_id = nf_id("NF_STDERR");
    bridge_exit_id = nf_id("NF_EXIT");
    version = nf_probe_id("NF_VERSION");
    if (!version || !preserved() || bridge_regs[0] != (uint32_t)version) return 1;
    if (nf_probe_call(version) != 0x00010000L || !preserved()) return 2;
    if (nf_probe_id("CPU_BRIDGE_MISSING_FEATURE") != 0 || !preserved()) return 3;
    name = nf_id("NF_NAME");
    if (!name || nf_call(name, host_name, (long)sizeof(host_name)) != 6 ||
        host_name[0] != 'A' || host_name[1] != 'R' || host_name[2] != 'A' ||
        host_name[3] != 'n' || host_name[4] != 'y' || host_name[5] != 'M' ||
        host_name[6] != 0) return 4;
    nf_call(stderr_id, "CPU BRIDGE PASS NatFeat D/A/CCR/SP and stack arguments\r\n");

    old_vector = (void (*)(void))Setexc(47, bridge_trap_handler);
    trap_probe();
    (void)Setexc(47, old_vector);
    if (!preserved() || bridge_regs[0] != 0x10203040UL) return 5;
    if (bridge_trap_pc != bridge_expected_pc + CPU_BRIDGE_EXPECT_PC_DELTA ||
        (bridge_trap_sr & 0x201f) != 0x2015 ||
        bridge_trap_format != 47 * 4 ||
        bridge_frame_sp + 8 != bridge_before_sp) return 6;
    nf_call(stderr_id, "CPU BRIDGE PASS trap15 format0 SR/PC/vector/SP and RTE\r\n");

    bridge_trap_mode = 1;
    old_vector = (void (*)(void))Setexc(47, bridge_trap_handler);
    trap_user_probe();
    (void)Setexc(47, old_vector);
    if (!preserved_sp(bridge_super_sp) || !frame(0x0715, 47) ||
        bridge_frame_sp + 8 != bridge_super_sp ||
        bridge_before_sp != bridge_bank_top || bridge_after_bank != bridge_bank_top ||
        bridge_regs[0] != 0x10203040UL)
        return 7;
    nf_call(stderr_id, "CPU BRIDGE PASS user trap uses ISP; saved USP and RTE\r\n");

    bridge_trap_mode = 2;
    old_vector = (void (*)(void))Setexc(47, bridge_trap_handler);
    trap_master_probe();
    (void)Setexc(47, old_vector);
    if (!preserved_sp(bridge_super_sp) || !frame(0x3715, 47) ||
        bridge_frame_sp + 8 != bridge_bank_top ||
        bridge_before_sp != bridge_bank_top || bridge_after_bank != bridge_bank_top ||
        bridge_regs[0] != 0x10203040UL) {
        char report[320];
        snprintf(report, sizeof(report),
                 "CPU BRIDGE FAIL master SR=%04x frame=%04x PC=%08lx/%08lx "
                 "A7=%08lx ISP=%08lx frameSP=%08lx MSPtop=%08lx afterMSP=%08lx\r\n",
                 (unsigned)bridge_trap_sr, (unsigned)bridge_trap_format,
                 (unsigned long)bridge_trap_pc, (unsigned long)bridge_expected_pc,
                 (unsigned long)bridge_regs[15], (unsigned long)bridge_super_sp,
                 (unsigned long)bridge_frame_sp, (unsigned long)bridge_bank_top,
                 (unsigned long)bridge_after_bank);
        nf_call(stderr_id, report);
        first_error = 8;
    } else {
        nf_call(stderr_id, "CPU BRIDGE PASS master synchronous trap uses MSP and RTE\r\n");
    }

    bridge_trap_mode = 3;
    old_vector = (void (*)(void))Setexc(8, bridge_trap_handler);
    privilege_probe();
    (void)Setexc(8, old_vector);
    if (!preserved_sp(bridge_super_sp) || !frame(0x0715, 8) ||
        bridge_frame_sp + 8 != bridge_super_sp ||
        bridge_after_bank != bridge_bank_top || bridge_regs[0] != 0x10203040UL)
        return 9;
    nf_call(stderr_id, "CPU BRIDGE PASS user privilege fault PC and format0 recovery\r\n");

    bridge_trap_mode = 4;
    old_vector = (void (*)(void))Setexc(4, bridge_trap_handler);
    illegal_probe();
    (void)Setexc(4, old_vector);
    if (!preserved() || !frame(0x2715, 4) ||
        bridge_frame_sp + 8 != bridge_before_sp || bridge_regs[0] != 0x10203040UL)
        return 10;
    nf_call(stderr_id, "CPU BRIDGE PASS illegal opcode fault PC and format0 recovery\r\n");
    return first_error;
}

int main(void)
{
    long rc = Supexec(checks);
    nf_call(nf_id("NF_STDERR"), rc ? "CPU BRIDGE FAIL\r\n" : "CPU BRIDGE PASS all checks\r\n");
    /* NF_EXIT preserves a nonzero test result in the actual host exit status. */
    nf_call(nf_id("NF_EXIT"), rc);
    return 1;
}
