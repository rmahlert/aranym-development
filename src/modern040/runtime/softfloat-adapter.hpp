/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <array>
#include <cstdint>
namespace modern040 {
using Extended96=std::array<std::uint32_t,3>;
inline std::uint32_t accrued_exceptions(std::uint32_t fpsr, std::uint32_t status) {
    std::uint32_t accrued = fpsr & 0xf8;
    constexpr unsigned snan = 0x4000, operr = 0x2000, overflow = 0x1000;
    constexpr unsigned underflow = 0x0800, divide_zero = 0x0400;
    constexpr unsigned inexact2 = 0x0200, inexact1 = 0x0100;
    if (status & (snan | operr)) accrued |= 0x80;
    if (status & overflow) accrued |= 0x40;
    if ((status & underflow) && (status & inexact2)) accrued |= 0x20;
    if (status & divide_zero) accrued |= 0x10;
    if (status & (overflow | inexact2 | inexact1)) accrued |= 0x08;
    return accrued;
}
enum class FpuBinary { Add,Sub,Mul,Div };
struct FpuValue { Extended96 words{};std::uint32_t status=0; };
struct FpuExecution {
    FpuValue value{};
    bool write_destination=false;
    bool write_secondary=false;
    std::uint8_t secondary_register=0;
    Extended96 secondary{};
    bool write_quotient=false;
    std::uint32_t quotient=0;
};
struct FpuStore { std::array<std::uint32_t,3> words{};std::uint32_t status=0; };
void softfloat_initialize();
FpuValue softfloat_binary(FpuBinary,const Extended96&,const Extended96&,std::uint32_t fpcr);
FpuValue softfloat_sqrt(const Extended96&,std::uint32_t fpcr);
/* Execute the arithmetic opmodes implemented in 68040 hardware.  This is the
 * pinned backend operation seam; guest decode and exception ordering stay in
 * the isolated core.  Returns false for 68881-only/undefined opmodes. */
bool softfloat_execute_040(std::uint8_t opmode,const Extended96&destination,
                          const Extended96&source,std::uint32_t fpcr,
                          std::uint32_t fpsr,FpuExecution&);
bool softfloat_constant(std::uint8_t,std::uint32_t,FpuValue&);
FpuValue softfloat_from_integer(std::int32_t);
FpuValue softfloat_from_single(std::uint32_t);
FpuValue softfloat_from_double(std::uint32_t high,std::uint32_t low);
FpuValue softfloat_from_extended(std::uint32_t exponent,std::uint32_t high,std::uint32_t low);
bool softfloat_store(std::uint8_t format,const Extended96&,std::uint32_t fpcr,FpuStore&);
}
