/* SPDX-License-Identifier: GPL-2.0-or-later
 * Test input corpus, also compiled against the preserved pre-change headers.
 * Expectations are pinned producer-boundary markers, not host assertions or
 * a claim of full silicon write-buffer/pipeline behavior.
 */
#pragma once
#include <array>
namespace producer_cases {
struct Case {const char*name;std::array<unsigned,2> words;unsigned count,writes,a0;bool completed,privileged;};
inline constexpr Case cases[]={
 {"move-b",{0x1080,0},1,1,0x5000,true,false},{"move-w",{0x3080,0},1,1,0x5000,true,false},{"move-l",{0x2080,0},1,1,0x5000,true,false},
 {"not-b",{0x4610,0},1,1,0x5000,true,false},{"not-w",{0x4650,0},1,1,0x5000,true,false},{"not-l",{0x4690,0},1,1,0x5000,true,false},
 {"moves-b",{0x0e10,0x0800},2,1,0x5000,true,true},{"moves-w",{0x0e50,0x0800},2,1,0x5000,true,true},{"moves-l",{0x0e90,0x0800},2,1,0x5000,true,true},
 {"jsr",{0x4e90,0},1,1,0x400,false,false},{"bsr",{0x6100,0x02fe},2,1,0x5000,false,false},
 {"movep-w",{0x0188,0},2,2,0x5000,false,false},{"movep-l",{0x01c8,0},2,4,0x5000,false,false},
 {"bfchg-1",{0xead0,0x0008},2,1,0x5000,false,false},{"bfchg-3",{0xead0,0x01d1},2,2,0x5000,false,false},{"bfchg-5",{0xead0,0x01c0},2,2,0x5000,false,false},
 {"bfclr-1",{0xecd0,0x0008},2,1,0x5000,false,false},{"bfclr-3",{0xecd0,0x01d1},2,2,0x5000,false,false},{"bfclr-5",{0xecd0,0x01c0},2,2,0x5000,false,false},
 {"bfset-1",{0xeed0,0x0008},2,1,0x5000,false,false},{"bfset-3",{0xeed0,0x01d1},2,2,0x5000,false,false},{"bfset-5",{0xeed0,0x01c0},2,2,0x5000,false,false},
 {"bfins-1",{0xefd0,0x0008},2,1,0x5000,false,false},{"bfins-3",{0xefd0,0x01d1},2,2,0x5000,false,false},{"bfins-5",{0xefd0,0x01c0},2,2,0x5000,false,false},
 {"movem-w",{0x4890,0x0003},2,2,0x5000,false,false},{"movem-l",{0x48d0,0x0003},2,2,0x5000,false,false}};
}
