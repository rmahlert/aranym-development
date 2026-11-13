/* SPDX-License-Identifier: GPL-2.0-or-later
 * Isolated architecture prototype. Not included by the production core.
 * MC68040UM Figure2-2, Figure8-6, Tables8-5/8-6 (PDF44/237/240/244).
 */
#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace writeback_prototype {
// Deliberately separate from BusCompletion: no transfer-result-to-stage cast.
enum class Origin : std::uint8_t { Unspecified=0, ExternalBus=0x40, AtcTranslation=0x41 };
struct Evidence {
 Origin origin=Origin::Unspecified;
 // Certification covers the whole original B/W/L write, not merely one
 // failed beat after an earlier fragment reached a device. Split/partial
 // operand recovery requires a separate architecture-backed model.
 bool no_commit_certified=false, instruction_complete=false, whole_scalar=false;
 unsigned size=0, function_code=0, transfer_type=0, pending_writes=0;
 std::uint32_t logical_address=0, value=0;
 std::optional<std::uint32_t> physical_address;
};
struct Entry {std::uint16_t status=0;std::uint32_t address=0,data=0;};
struct Layout {std::uint16_t ssw=0;std::uint32_t fault_address=0;std::array<Entry,3> wb{};};
inline std::optional<Layout> single_completed_write(const Evidence& e){
 if(!e.no_commit_certified||!e.instruction_complete||!e.whole_scalar||e.pending_writes!=1||
    (e.size!=1&&e.size!=2&&e.size!=4)||(e.function_code!=1&&e.function_code!=5)||e.transfer_type)return {};
 const auto mask=e.size==4?UINT32_MAX:(std::uint32_t{1}<<(8*e.size))-1;
 const auto data=e.value&mask;const auto size=e.size==1?0x20:e.size==2?0x40:0;
 Layout out;out.ssw=std::uint16_t(size|e.function_code);out.fault_address=e.logical_address;
 if(e.origin==Origin::ExternalBus){
  if(!e.physical_address)return {};
  // Place each logical byte onto its big-endian physical bus lane. Unused
  // lanes are deterministically zero; they are not another pending write.
  std::uint32_t aligned=0;
  for(unsigned i=0;i<e.size;++i){
   const unsigned lane=((*e.physical_address&3)+i)&3;
   const auto byte=(data>>(8*(e.size-1-i)))&255;
   aligned|=byte<<(24-8*lane);
  }
  out.wb[0]={std::uint16_t(0x80|size|e.function_code),*e.physical_address,aligned};
 }else if(e.origin==Origin::AtcTranslation){
  // This models a supplied ATC-origin frame only, not an MMU or table walk.
  // A physical address is unnecessary and must not replace the logical one.
  out.ssw|=0x0400;
  out.wb[1]={std::uint16_t(0x80|size|e.function_code),e.logical_address,data};
 }else return {};
 return out;
}
inline std::array<std::uint8_t,60> frame(const Layout& layout,std::uint16_t sr,std::uint32_t next_pc){
 std::array<std::uint8_t,60> bytes{};
 const auto put=[&](unsigned offset,unsigned size,std::uint32_t value){for(unsigned i=size;i;--i){bytes.at(offset+i-1)=std::uint8_t(value);value>>=8;}};
 put(0,2,sr);put(2,4,next_pc);put(6,2,0x7008);put(12,2,layout.ssw);put(20,4,layout.fault_address);
 put(18,2,layout.wb[0].status);put(40,4,layout.wb[0].address);put(44,4,layout.wb[0].data);
 put(16,2,layout.wb[1].status);put(32,4,layout.wb[1].address);put(36,4,layout.wb[1].data);
 put(14,2,layout.wb[2].status);put(24,4,layout.wb[2].address);put(28,4,layout.wb[2].data);
 return bytes;
}
}
