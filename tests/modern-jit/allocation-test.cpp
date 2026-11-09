/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ordinary allocator unit test, linked to the emulator's actual vm_alloc.o.
 * Uses only a page allocated by this test; no other process or mapping is read.
 */
#include "vm_alloc.h"
#include <cstdio>
#include <cstring>
int main() {
    if(vm_init()!=0)return 1;
    const size_t bytes=static_cast<size_t>(getpagesize());
    void *page=vm_acquire(bytes);
    if(page==VM_MAP_FAILED)return 2;
    std::memset(page,0xa5,bytes);
    const bool replaced=vm_acquire_fixed(page,bytes);
    bool unchanged=true;
    for(size_t i=0;i<bytes;++i)if(static_cast<unsigned char *>(page)[i]!=0xa5)unchanged=false;
    if(replaced||!unchanged){std::fputs("FAIL occupied mapping replaced or changed\n",stderr);return 3;}
    static_cast<unsigned char *>(page)[0]=0x5a;
    if(static_cast<unsigned char *>(page)[0]!=0x5a)return 4;
    vm_release(page,bytes);
    if(!vm_acquire_fixed(page,bytes))return 5;
    for(size_t i=0;i<bytes;++i)if(static_cast<unsigned char *>(page)[i]!=0)return 6;
    vm_release(page,bytes);
    if(vm_acquire_fixed(page,bytes,VM_MAP_SHARED))return 7;
    vm_exit();
    std::puts("PASS occupied reservation rejected without byte/protection changes; released reservation succeeds; shared request rejected");
}
