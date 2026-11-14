/* SPDX-License-Identifier: GPL-2.0-or-later
 * Narrow adapter around the pinned WinUAE SoftFloat backend.  No WinUAE CPU,
 * memory or scheduler global crosses this boundary.
 */
#include "softfloat-adapter.hpp"
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "fpp.h"
#include <stdexcept>

/* Storage normally owned by fpp.cpp.  The isolated core intentionally links
 * only the arithmetic backend, so keep its function-vector storage here.
 */
FPP_PRINT fpp_print;
FPP_IS fpp_unset_snan,fpp_is_init,fpp_is_snan,fpp_is_nan,fpp_is_infinity,
    fpp_is_zero,fpp_is_neg,fpp_is_denormal,fpp_is_unnormal;
FPP_A fpp_fix_infinity,fpp_normalize,fpp_get_internal_overflow,
    fpp_get_internal_underflow,fpp_get_internal_round_all,fpp_get_internal_round,
    fpp_get_internal_round_exten,fpp_get_internal,fpp_round_single,
    fpp_round_double,fpp_round32,fpp_round64;
FPP_GET_STATUS fpp_get_status;
FPP_CLEAR_STATUS fpp_clear_status;
FPP_SET_MODE fpp_set_mode;
FPP_SUPPORT_FLAGS fpp_get_support_flags;
FPP_FROM_NATIVE fpp_from_native;
FPP_TO_NATIVE fpp_to_native;
FPP_TO_INT fpp_to_int;
FPP_FROM_INT fpp_from_int;
FPP_PACK fpp_to_pack,fpp_from_pack;
FPP_TO_SINGLE fpp_to_single;
FPP_FROM_SINGLE fpp_from_single;
FPP_TO_DOUBLE fpp_to_double;
FPP_FROM_DOUBLE fpp_from_double;
FPP_TO_EXTEN fpp_to_exten,fpp_to_exten_fmovem;
FPP_FROM_EXTEN fpp_from_exten,fpp_from_exten_fmovem;
FPP_DENORMALIZE fpp_denormalize;
FPP_GET32 fpp_get_internal_grs;
FPP_AB fpp_int,fpp_sinh,fpp_intrz,fpp_lognp1,fpp_etoxm1,fpp_tanh,fpp_atan,
    fpp_atanh,fpp_sin,fpp_asin,fpp_tan,fpp_etox,fpp_twotox,fpp_tentox,
    fpp_logn,fpp_log10,fpp_log2,fpp_cosh,fpp_acos,fpp_cos,fpp_getexp,
    fpp_getman,fpp_scale,fpp_sgldiv,fpp_sglmul,fpp_cmp,fpp_tst;
FPP_ABP fpp_sqrt,fpp_abs,fpp_neg,fpp_div,fpp_add,fpp_mul,fpp_sub,fpp_move;
FPP_ABC fpp_sincos;
FPP_ABQS fpp_mod,fpp_rem;

namespace modern040 {
namespace {
bool initialized=false;
fpdata decode(const Extended96&w) {
    fpdata value{};fpp_to_exten_fmovem(&value,w[0],w[1],w[2]);return value;
}
FpuValue encode(fpdata&value) {
    FpuValue out;fpp_from_exten_fmovem(&value,&out.words[0],&out.words[1],&out.words[2]);
    fpp_get_status(&out.status);return out;
}
void begin(std::uint32_t fpcr) {
    softfloat_initialize();fpp_set_mode(fpcr);fpp_clear_status();
}
}
void softfloat_initialize() {
    if(!initialized){fp_init_softfloat(68040);initialized=true;}
}
FpuValue softfloat_binary(FpuBinary operation,const Extended96&a,const Extended96&b,std::uint32_t fpcr) {
    begin(fpcr);auto destination=decode(a);auto source=decode(b);
    switch(operation) {
    case FpuBinary::Add:fpp_add(&destination,&source,0);break;
    case FpuBinary::Sub:fpp_sub(&destination,&source,0);break;
    case FpuBinary::Mul:fpp_mul(&destination,&source,0);break;
    case FpuBinary::Div:fpp_div(&destination,&source,0);break;
    default:throw std::invalid_argument("unknown FPU binary operation");
    }
    return encode(destination);
}
FpuValue softfloat_sqrt(const Extended96&source,std::uint32_t fpcr) {
    begin(fpcr);fpdata destination{},operand=decode(source);fpp_sqrt(&destination,&operand,0);return encode(destination);
}
bool softfloat_execute_040(std::uint8_t opmode,const Extended96&destination_words,
                          const Extended96&source_words,std::uint32_t fpcr,
                          std::uint32_t fpsr,FpuExecution&out) {
    begin(fpcr);auto destination=decode(destination_words);auto source=decode(source_words);
    int precision=PREC_NORMAL;unsigned long long quotient=(fpsr>>16)&0x7f;std::uint8_t quotient_sign=bool(fpsr&0x00800000);
    switch(opmode) {
    case 0x40: precision=PREC_FLOAT; [[fallthrough]];
    case 0x00: fpp_move(&destination,&source,precision);out.write_destination=true;break;
    case 0x44: fpp_move(&destination,&source,PREC_DOUBLE);out.write_destination=true;break;
    case 0x41: precision=PREC_FLOAT; [[fallthrough]];
    case 0x04:case 0x05:fpp_sqrt(&destination,&source,precision);out.write_destination=true;break;
    case 0x45:fpp_sqrt(&destination,&source,PREC_DOUBLE);out.write_destination=true;break;
    case 0x01:fpp_int(&destination,&source);out.write_destination=true;break;
    case 0x02:fpp_sinh(&destination,&source);out.write_destination=true;break;
    case 0x03:fpp_intrz(&destination,&source);out.write_destination=true;break;
    case 0x06:case 0x07:fpp_lognp1(&destination,&source);out.write_destination=true;break;
    case 0x08:fpp_etoxm1(&destination,&source);out.write_destination=true;break;
    case 0x09:fpp_tanh(&destination,&source);out.write_destination=true;break;
    case 0x0a:case 0x0b:fpp_atan(&destination,&source);out.write_destination=true;break;
    case 0x0c:fpp_asin(&destination,&source);out.write_destination=true;break;
    case 0x0d:fpp_atanh(&destination,&source);out.write_destination=true;break;
    case 0x0e:fpp_sin(&destination,&source);out.write_destination=true;break;
    case 0x0f:fpp_tan(&destination,&source);out.write_destination=true;break;
    case 0x10:fpp_etox(&destination,&source);out.write_destination=true;break;
    case 0x11:fpp_twotox(&destination,&source);out.write_destination=true;break;
    case 0x12:case 0x13:fpp_tentox(&destination,&source);out.write_destination=true;break;
    case 0x14:fpp_logn(&destination,&source);out.write_destination=true;break;
    case 0x15:fpp_log10(&destination,&source);out.write_destination=true;break;
    case 0x16:case 0x17:fpp_log2(&destination,&source);out.write_destination=true;break;
    case 0x58:precision=PREC_FLOAT; [[fallthrough]];
    case 0x18:fpp_abs(&destination,&source,precision);out.write_destination=true;break;
    case 0x5c:fpp_abs(&destination,&source,PREC_DOUBLE);out.write_destination=true;break;
    case 0x19:fpp_cosh(&destination,&source);out.write_destination=true;break;
    case 0x5a:precision=PREC_FLOAT; [[fallthrough]];
    case 0x1a:case 0x1b:fpp_neg(&destination,&source,precision);out.write_destination=true;break;
    case 0x5e:fpp_neg(&destination,&source,PREC_DOUBLE);out.write_destination=true;break;
    case 0x1c:fpp_acos(&destination,&source);out.write_destination=true;break;
    case 0x1d:fpp_cos(&destination,&source);out.write_destination=true;break;
    case 0x1e:fpp_getexp(&destination,&source);out.write_destination=true;break;
    case 0x1f:fpp_getman(&destination,&source);out.write_destination=true;break;
    case 0x60:precision=PREC_FLOAT; [[fallthrough]];
    case 0x20:fpp_div(&destination,&source,precision);out.write_destination=true;break;
    case 0x64:fpp_div(&destination,&source,PREC_DOUBLE);out.write_destination=true;break;
    case 0x21:fpp_mod(&destination,&source,&quotient,&quotient_sign);out.write_destination=true;out.write_quotient=true;break;
    case 0x62:precision=PREC_FLOAT; [[fallthrough]];
    case 0x22:fpp_add(&destination,&source,precision);out.write_destination=true;break;
    case 0x66:fpp_add(&destination,&source,PREC_DOUBLE);out.write_destination=true;break;
    case 0x63:precision=PREC_FLOAT; [[fallthrough]];
    case 0x23:fpp_mul(&destination,&source,precision);out.write_destination=true;break;
    case 0x67:fpp_mul(&destination,&source,PREC_DOUBLE);out.write_destination=true;break;
    case 0x24:fpp_sgldiv(&destination,&source);out.write_destination=true;break;
    case 0x25:fpp_rem(&destination,&source,&quotient,&quotient_sign);out.write_destination=true;out.write_quotient=true;break;
    case 0x26:fpp_scale(&destination,&source);out.write_destination=true;break;
    case 0x27:fpp_sglmul(&destination,&source);out.write_destination=true;break;
    case 0x68:precision=PREC_FLOAT; [[fallthrough]];
    case 0x28:case 0x29:case 0x2a:case 0x2b:case 0x2c:case 0x2d:case 0x2e:case 0x2f:
        fpp_sub(&destination,&source,precision);out.write_destination=true;break;
    case 0x6c:fpp_sub(&destination,&source,PREC_DOUBLE);out.write_destination=true;break;
    case 0x30:case 0x31:case 0x32:case 0x33:case 0x34:case 0x35:case 0x36:case 0x37: {
        fpdata secondary{};fpp_sincos(&destination,&source,&secondary);out.write_destination=true;
        out.write_secondary=true;out.secondary_register=opmode&7;out.secondary=encode(secondary).words;break;
    }
    case 0x38:case 0x39:case 0x3c:case 0x3d:
        fpp_cmp(&destination,&source);out.write_destination=false;break;
    case 0x3a:case 0x3b:case 0x3e:case 0x3f:
        fpp_tst(&destination,&source);out.write_destination=false;break;
    default:return false;
    }
    if(out.write_quotient)out.quotient=((std::uint32_t(quotient)<<16)&0x007f0000u)|(quotient_sign?0x00800000u:0);
    out.value=encode(destination);return true;
}
FpuValue softfloat_from_integer(std::int32_t source) {
    begin(0);fpdata value{};fpp_from_int(&value,source);return encode(value);
}
FpuValue softfloat_from_single(std::uint32_t source) {
    begin(0);fpdata value{};fpp_to_single(&value,source);return encode(value);
}
FpuValue softfloat_from_double(std::uint32_t high,std::uint32_t low) {
    begin(0);fpdata value{};fpp_to_double(&value,high,low);return encode(value);
}
FpuValue softfloat_from_extended(std::uint32_t exponent,std::uint32_t high,std::uint32_t low) {
    begin(0);fpdata value{};fpp_to_exten(&value,exponent,high,low);return encode(value);
}
bool softfloat_store(std::uint8_t format,const Extended96&source,std::uint32_t fpcr,FpuStore&out) {
    begin(fpcr);auto value=decode(source);
    switch(format) {
    case 0:out.words[0]=std::uint32_t(fpp_to_int(&value,2));break;
    case 1:out.words[0]=fpp_from_single(&value);break;
    case 2:fpp_from_exten(&value,&out.words[0],&out.words[1],&out.words[2]);break;
    case 4:out.words[0]=std::uint32_t(fpp_to_int(&value,1));break;
    case 5:fpp_from_double(&value,&out.words[0],&out.words[1]);break;
    case 6:out.words[0]=std::uint32_t(fpp_to_int(&value,0));break;
    default:return false;
    }
    fpp_get_status(&out.status);return true;
}
}
