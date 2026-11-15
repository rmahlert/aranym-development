// Standalone donor-emitter/platform test, not a 68k guest JIT.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#if defined(__aarch64__) || defined(__arm64__)
#define NATIVE_ARM64 1
#include <sys/mman.h>
#include <unistd.h>
#ifdef __APPLE__
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif
#endif

static std::vector<uint32_t> words;
static void emit_long(uint32_t word) { words.push_back(word); }
#include "../../src/uae_cpu/compiler/arm64/codegen_arm64.h"

static void require(bool ok, const char *message)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void encoding_test()
{
    words.clear();
    MOV_wi(0, 42);
    ADD_www(0, 0, 1);
    ADD_xxx(0, 0, 1);
    SUB_www(0, 0, 1);
    CMP_ww(0, 1);
    MRS_NZCV_x(0);
    LDR_xXi(2, 0, 8);
    STR_xXi(2, 0, 8);
    B_i(2);
    B_i(-1);
    RET;
    // A64 instruction encodings, independent of this header's macro expressions.
    const uint32_t expected[] = {
        0x52800540, 0x0b010000, 0x8b010000, 0x4b010000,
        0x6b01001f, 0xd53b4200, 0xf9400402, 0xf9000402,
        0x14000002, 0x17ffffff, 0xd65f03c0
    };
    require(words.size() == sizeof(expected) / sizeof(expected[0]), "encoding length");
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i] != expected[i]) {
            std::fprintf(stderr, "encoding[%zu]: %08x expected %08x\n",
                         i, words[i], expected[i]);
            require(false, "A64 encoding");
        }
    }
    std::puts("PASS: donor A64 instruction encodings");
}

#ifdef NATIVE_ARM64
class CodeRegion {
    void *memory_;
    size_t size_;
public:
    CodeRegion()
    {
        const long page = sysconf(_SC_PAGESIZE);
        require(page > 0, "host page size");
        size_ = static_cast<size_t>(page);
        int flags = MAP_PRIVATE | MAP_ANON;
        int prot = PROT_READ | PROT_WRITE;
#ifdef __APPLE__
        flags |= MAP_JIT;
        prot |= PROT_EXEC;
#endif
        memory_ = mmap(nullptr, size_, prot, flags, -1, 0);
        if (memory_ == MAP_FAILED) std::perror("mmap code region");
        require(memory_ != MAP_FAILED, "code allocation");
    }
    ~CodeRegion() { munmap(memory_, size_); }
    CodeRegion(const CodeRegion&) = delete;
    CodeRegion& operator=(const CodeRegion&) = delete;

    template<class Function> Function install()
    {
        const size_t bytes = words.size() * sizeof(uint32_t);
        require(bytes > 0 && bytes <= size_, "code fits allocation");
#ifdef __APPLE__
        pthread_jit_write_protect_np(0);
#else
        require(mprotect(memory_, size_, PROT_READ | PROT_WRITE) == 0, "enable write");
#endif
        std::memcpy(memory_, words.data(), bytes);
#ifdef __APPLE__
        pthread_jit_write_protect_np(1);
        sys_icache_invalidate(memory_, bytes);
#else
        require(mprotect(memory_, size_, PROT_READ | PROT_EXEC) == 0, "enable execute");
        __builtin___clear_cache(static_cast<char*>(memory_), static_cast<char*>(memory_) + bytes);
#endif
        return reinterpret_cast<Function>(memory_);
    }
};

static void execution_test()
{
    // One MAP_JIT region, reused for each program. Hardened runtime allows one.
    CodeRegion code;
    using Binary32 = uint32_t (*)(uint32_t, uint32_t);
    using Binary64 = uint64_t (*)(uint64_t, uint64_t);
    const uint32_t edges[] = {0, 1, 0x7fffffff, 0x80000000, 0xffffffff};
    words.clear(); ADD_www(0, 0, 1); RET;
    auto add = code.install<Binary32>();
    for (uint32_t a : edges) for (uint32_t b : edges)
        require(add(a, b) == uint32_t(a + b), "32-bit add/overflow");
    words.clear(); SUB_www(0, 0, 1); RET;
    auto sub = code.install<Binary32>();
    for (uint32_t a : edges) for (uint32_t b : edges)
        require(sub(a, b) == uint32_t(a - b), "32-bit subtract/overflow");

    words.clear(); ADD_xxx(0, 0, 1); RET;
    auto add64 = code.install<Binary64>();
    require(add64(UINT64_C(0x100000000), UINT64_C(0x200000001)) == UINT64_C(0x300000001),
            "64-bit operands preserved");

    words.clear(); CMP_ww(0, 1); MRS_NZCV_x(0); RET;
    auto flags = code.install<Binary64>();
    require(flags(0, 0) == UINT64_C(0x60000000), "compare equal NZCV");
    require(flags(0, 1) == UINT64_C(0x80000000), "compare negative NZCV");
    require(flags(0x80000000, 1) == UINT64_C(0x30000000), "compare overflow NZCV");

    // Pointer arguments stay 64-bit. Copy a full 64-bit word through generated code.
    words.clear(); LDR_xXi(2, 0, 8); STR_xXi(2, 1, 0); MOV_xx(0, 2); RET;
    using Copy = uint64_t (*)(const uint64_t*, uint64_t*);
    auto copy = code.install<Copy>();
    const uint64_t source[] = {0, UINT64_C(0xfedcba9876543210)};
    uint64_t target = 0;
    require(copy(source, &target) == source[1] && target == source[1], "load/store via host pointers");

    // Loop down from a small count using backward CBNZ, then a forward branch.
    words.clear(); MOV_ww(1, 0); MOV_wi(0, 0); CBZ_wi(1, 4);
    ADD_www(0, 0, 1); SUB_wwi(1, 1, 1); CBNZ_wi(1, -2); B_i(2); MOV_wi(0, 0xffff); RET;
    using Unary = uint32_t (*)(uint32_t);
    auto sum = code.install<Unary>();
    require(sum(0) == 0 && sum(10) == 55 && sum(100) == 5050, "forward/backward branches");

    // Repeated rewrite at exactly the same address detects stale instruction caches.
    using Constant = uint32_t (*)();
    for (uint32_t value = 0; value < 4096; ++value) {
        words.clear(); MOV_wi(0, value); RET;
        require(code.install<Constant>()() == value, "rewrite/cache invalidation");
    }
    std::puts("PASS: native ARM64 arithmetic, NZCV, pointers, branches and 4096 cache rewrites");
}
#endif

int main(int argc, char **argv)
{
    bool require_native = false;
    if (argc == 2 && std::strcmp(argv[1], "--require-native") == 0) require_native = true;
    else require(argc == 1, "usage: platform-test [--require-native]");
    encoding_test();
#ifdef NATIVE_ARM64
    (void)require_native;
    execution_test();
#else
    require(!require_native, "native ARM64 execution required; this host is not ARM64");
    std::puts("SKIP: native ARM64 execution (encoding check only)");
#endif
    return 0;
}
