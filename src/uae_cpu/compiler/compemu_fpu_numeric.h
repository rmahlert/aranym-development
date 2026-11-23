#ifndef COMPEMU_FPU_NUMERIC_H
#define COMPEMU_FPU_NUMERIC_H

static ALWAYS_INLINE bool native_fpu_multiply(NativeFpuOperand a,NativeFpuOperand b,
        NativeFpuOperand &out,bool &inexact) {
    if (!a.supported() || !b.supported()) return false;
    const unsigned sign=(a.sign_exponent^b.sign_exponent)&0x8000;
    if (!a.mantissa || !b.mantissa) { out={0,uae_u16(sign)}; return true; }
    const unsigned __int128 product=static_cast<unsigned __int128>(a.mantissa)*b.mantissa;
    const unsigned carry=product>>127;
    const unsigned shift=63+carry;
    int exponent=int(a.exponent())+int(b.exponent())-16383+carry;
    uae_u64 rounded=product>>shift;
    const uae_u64 half=uae_u64(1)<<(shift-1);
    const uae_u64 remainder=uae_u64(product)&((half-1)|half);
    inexact=remainder!=0;
    if ((remainder>half || (remainder==half && (rounded&1))) && ++rounded==0) {
        rounded=uae_u64(1)<<63;
        ++exponent;
    }
    if (exponent<=0 || exponent>=0x7fff) return false;
    out={rounded,uae_u16(sign|exponent)};
    return true;
}

static ALWAYS_INLINE bool native_fpu_sqrt(NativeFpuOperand input,
        NativeFpuOperand &out,bool &inexact) {
    if (!input.supported()) return false;
    if (!input.mantissa) { out=input; return true; }
    if (input.negative()) return false;
    const unsigned odd=(input.exponent()^1)&1;
    const double scale=odd?0x1p61:0x1p60;
    const uae_u64 half=input.mantissa>>1;
    const uae_u32 control=0x1f80;
    uae_u32 environment;
    uae_u64 seed;
    __asm__ volatile("stmxcsr %0\n\tldmxcsr %3\n\tcvtsi2sdq %2,%%xmm0\n\tmulsd %4,%%xmm0\n\tsqrtsd %%xmm0,%%xmm0\n\tcvttsd2siq %%xmm0,%1\n\tldmxcsr %0"
        :"=m"(environment),"=r"(seed):"r"(half),"m"(control),"m"(scale):"xmm0","memory");
    using Wide=unsigned __int128;
    const Wide radicand=Wide(input.mantissa)<<(63+odd);
    uae_u64 root=seed>=(uae_u64(1)<<62)?~uae_u64(0):seed<<2;
    const Wide corrected=(Wide(root)+radicand/root)>>1;
    root=corrected>~uae_u64(0)?~uae_u64(0):uae_u64(corrected);
    Wide square=Wide(root)*root;
    if (square>radicand) {
        --root;
        square=Wide(root)*root;
        if (square>radicand) return false;
    } else if (root!=~uae_u64(0) && Wide(root+1)*(root+1)<=radicand) {
        ++root;
        square=Wide(root)*root;
        if (root!=~uae_u64(0) && Wide(root+1)*(root+1)<=radicand) return false;
    }
    const Wide remainder=radicand-square;
    unsigned exponent=(input.exponent()+16383)/2;
    if (remainder>root && ++root==0) { root=uae_u64(1)<<63; ++exponent; }
    out={root,uae_u16(exponent)};
    inexact=remainder!=0;
    return true;
}

static ALWAYS_INLINE bool native_fpu_divide(NativeFpuOperand a,NativeFpuOperand b,
        NativeFpuOperand &out,bool &inexact) {
    if (!a.supported() || !b.supported() || !b.mantissa) return false;
    const unsigned sign=(a.sign_exponent^b.sign_exponent)&0x8000;
    if (!a.mantissa) { out={0,uae_u16(sign)}; return true; }
    const bool normalize=a.mantissa<b.mantissa;
    const unsigned __int128 dividend=static_cast<unsigned __int128>(a.mantissa)<<(63+normalize);
    const uae_u64 high=dividend>>64;
    if (high>=b.mantissa) return false;
    uae_u64 quotient,remainder;
    __asm__("divq %4":"=a"(quotient),"=d"(remainder)
        :"0"(uae_u64(dividend)),"1"(high),"r"(b.mantissa):"cc");
    int exponent=int(a.exponent())-int(b.exponent())+16383-normalize;
    inexact=remainder!=0;
    const uae_u64 complement=b.mantissa-remainder;
    if ((remainder>complement || (remainder==complement && (quotient&1))) && ++quotient==0) {
        quotient=uae_u64(1)<<63;
        ++exponent;
    }
    if (exponent<=0 || exponent>=0x7fff) return false;
    out={quotient,uae_u16(sign|exponent)};
    return true;
}

#endif
