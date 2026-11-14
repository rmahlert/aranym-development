// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef ARANYM_MODERN040_PERFORMANCE_H
#define ARANYM_MODERN040_PERFORMANCE_H
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <algorithm>
#include "display_performance.h"
inline bool aranym_modern_perf_enabled(){static const bool enabled=[](){const char *p=std::getenv("ARANYM_MODERN040_PERFORMANCE");return p&&std::strcmp(p,"1")==0;}();return enabled;}
struct AranymModernPerf {
    Uint64 calls[6]{},samples[6]{},elapsed[6]{},memory[3]{},services[6]{},events[13]{},opcodes[65536]{};
    Uint64 interpreter=0,fallback=0,get_state=0,set_state=0;
    unsigned random=0x6d2b79f5;bool sampling=false;
    std::map<Uint64,Uint64> sampled_sites;Uint64 site_drops=0,reject[6]{};unsigned reject_lines=0;
    ~AranymModernPerf(){
        if(!aranym_modern_perf_enabled())return;
        std::fprintf(stderr,"MODERN040_PERF interpreter=%llu fallback=%llu get_state=%llu set_state=%llu fetch=%llu read=%llu write=%llu\n",(unsigned long long)interpreter,(unsigned long long)fallback,(unsigned long long)get_state,(unsigned long long)set_state,(unsigned long long)memory[0],(unsigned long long)memory[1],(unsigned long long)memory[2]);
        const char*names[]={"import","run","export","memory","service","ticks"};
        for(unsigned i=0;i<6;++i)std::fprintf(stderr,"MODERN040_PERF_GROUP group=%s calls=%llu samples=%llu sampled_us=%llu\n",names[i],(unsigned long long)calls[i],(unsigned long long)samples[i],(unsigned long long)elapsed[i]);
        const char*eventnames[]={"compile_block","recompile","cache_miss","invalidate_block","flush_hard","flush_lazy","flush_none","service_exception","fallback_end_compile","memory_barrier","import_cache_hit","import_cache_miss","import_cache_verify"};
        for(unsigned i=0;i<13;++i)std::fprintf(stderr,"MODERN040_PERF_EVENT event=%s count=%llu\n",eventnames[i],(unsigned long long)events[i]);
        for(unsigned i=1;i<6;++i)std::fprintf(stderr,"MODERN040_PERF_SERVICE kind=%u count=%llu\n",i,(unsigned long long)services[i]);
        const char*reasons[]={"not-admitted","warmup","code-outside-tt-ram","control-flow","flags","emitter"};
        for(unsigned i=0;i<6;++i)std::fprintf(stderr,"MODERN040_PERF_REJECTION reason=%s count=%llu\n",reasons[i],(unsigned long long)reject[i]);
        std::vector<std::pair<Uint64,Uint64>> sites;for(const auto &entry:sampled_sites)sites.emplace_back(entry.second,entry.first);
        std::sort(sites.rbegin(),sites.rend());
        for(unsigned i=0;i<sites.size()&&i<16;++i)std::fprintf(stderr,"MODERN040_PERF_SITE opcode=%04x pc=%08x samples=%llu dropped_sites=%llu\n",unsigned(sites[i].second>>32),unsigned(sites[i].second),(unsigned long long)sites[i].first,(unsigned long long)site_drops);
        for(unsigned i=0;i<16;++i){unsigned best=0;for(unsigned j=1;j<65536;++j)if(opcodes[j]>opcodes[best])best=j;if(!opcodes[best])break;std::fprintf(stderr,"MODERN040_PERF_OPCODE opcode=%04x count=%llu\n",best,(unsigned long long)opcodes[best]);opcodes[best]=0;}
    }
};
inline AranymModernPerf &aranym_modern_perf(){static AranymModernPerf p;return p;}
inline void aranym_modern_perf_rejection(unsigned opcode,unsigned pc,unsigned reason,unsigned smode,unsigned dmode){
    if(!aranym_modern_perf_enabled())return;auto &p=aranym_modern_perf();++p.reject[reason];
    if(p.reject_lines<64&&(opcode==0x2038||(opcode&0xfb80)==0x4880)){
        ++p.reject_lines;std::fprintf(stderr,"MODERN040_PERF_REJECT opcode=%04x pc=%08x reason=%u smode=%u dmode=%u\n",opcode,pc,reason,smode,dmode);}
}
inline void aranym_modern_perf_event(unsigned event){if(aranym_modern_perf_enabled())++aranym_modern_perf().events[event];}
class AranymModernPerfStep {
    bool active,previous=false;
public:
    explicit AranymModernPerfStep(bool jit,unsigned opcode=0,unsigned pc=0):active(aranym_modern_perf_enabled()){
        if(!active)return;auto &p=aranym_modern_perf();previous=p.sampling;
        p.random=p.random*1664525u+1013904223u;p.sampling=(p.random>>20)==0;
        if(jit){++p.fallback;++p.opcodes[opcode&65535];if(p.sampling){const Uint64 key=(Uint64(opcode&65535)<<32)|pc;if(p.sampled_sites.size()<4096||p.sampled_sites.count(key))++p.sampled_sites[key];else ++p.site_drops;}}else ++p.interpreter;
    }
    ~AranymModernPerfStep(){if(active)aranym_modern_perf().sampling=previous;}
};
class AranymModernPerfScope {
    unsigned group;bool active,sampled=false;Uint64 start=0;
public:
    explicit AranymModernPerfScope(unsigned g):group(g),active(aranym_modern_perf_enabled()){
        if(active){auto &p=aranym_modern_perf();++p.calls[g];sampled=p.sampling;if(sampled)start=aranym_display_perf_clock();}}
    ~AranymModernPerfScope(){if(sampled){auto &p=aranym_modern_perf();++p.samples[group];p.elapsed[group]+=aranym_display_perf_clock()-start;}}
};
#endif
