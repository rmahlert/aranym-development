/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_DSP_HOST_H
#define ARANYM_DSP_HOST_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned dsp_host_trace_mask;
void dsp_host_trace(unsigned mask, const char *format, ...);
int dsp_host_trace_enabled(unsigned mask);
void dsp_host_warn(int level, const char *format, ...);
int dsp_host_debug_enabled(void);
void dsp_host_debugger(void);
void dsp_host_ssi_receive_request(void);
void dsp_host_ssi_transmit_frame(uint32_t frame);
#ifdef __cplusplus
}
#endif
#define TRACE_DSP_STATE          0x01u
#define TRACE_DSP_HOST_SSI       0x02u
#define TRACE_DSP_HOST_INTERFACE 0x04u
#define TRACE_DSP_HOST_COMMAND   0x08u
#define TRACE_DSP_INTERRUPT      0x10u
#define TRACE_DSP_DISASM         0x20u
#define TRACE_DSP_DISASM_REG     0x40u
#define TRACE_DSP_DISASM_MEM     0x80u
#define LOG_TRACE(mask, ...) do { if (dsp_host_trace_mask & (mask)) dsp_host_trace((mask), __VA_ARGS__); } while (0)
#define LOG_TRACE_LEVEL(mask) (dsp_host_trace_mask & (mask))
#define LOG_TRACE_DIRECT_INIT() ((void)0)
#define TraceFile stderr
#define Log_Printf dsp_host_warn
#define LOG_WARN 1
#define DSP_SsiTransmit_SC1 dsp_host_ssi_receive_request
#define DSP_SsiTransmit_SC2 dsp_host_ssi_transmit_frame
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif
#endif
