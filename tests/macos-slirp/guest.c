/* Owned packet-level NatFeat Ethernet acceptance guest. GPL-2.0-or-later. */
#include "../../atari/network/ethernet/ethernet_nfapi.h"
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
extern long nf_id(const char *);
extern long nf_call(long, ...);
static long nfEtherID, stderr_id, shutdown_id;
static u8 tx[1600], rx[1600], mac[6], gateway[6];
static u32 guest_ip, host_ip;
/* The host runner patches these unique data values to its ephemeral ports. */
static volatile u32 http_port = 0xa55a1234UL;
static volatile u32 udp_port = 0xa55a5678UL;
static volatile u32 *const ticks = (volatile u32 *)0x4baL;
static int rxlen;

static void zero(u8 *p, int n) { while (n--) *p++ = 0; }
static void copy(u8 *p, const u8 *s, int n) { while (n--) *p++ = *s++; }
void *memcpy(void *p,const void *s,unsigned long n) { copy((u8 *)p,(const u8 *)s,(int)n);return p; }
static int equal(const u8 *a, const u8 *b, int n) { while (n--) if (*a++ != *b++) return 0; return 1; }
static int length(const char *s) { int n=0; while (s[n]) ++n; return n; }
static u16 r16(const u8 *p) { return (u16)((p[0]<<8)|p[1]); }
static u32 r32(const u8 *p) { return ((u32)r16(p)<<16)|r16(p+2); }
static void w16(u8 *p, u16 n) { p[0]=(u8)(n>>8); p[1]=(u8)n; }
static void w32(u8 *p, u32 n) { w16(p,(u16)(n>>16)); w16(p+2,(u16)n); }
static u32 sum(const u8 *p, int n) { u32 s=0; while(n>1){s+=r16(p);p+=2;n-=2;}if(n)s+=(u32)*p<<8;return s; }
static u16 finish(u32 s) { while(s>>16)s=(s&65535)+(s>>16);return (u16)~s; }
static void report(const char *s) { nf_call(stderr_id,s); }
static void fail(const char *s) {
    report("SLIRP_GUEST FAIL ");report(s);report("\r\n");
    nf_call(shutdown_id,0L); for (;;) {}
}
static void send_frame(int n) { nf_call(ETH(XIF_WRITEBLOCK),0L,tx,(long)n); }
static void ethernet(u16 kind, const u8 *dest) {
    zero(tx,sizeof(tx));copy(tx,dest,6);copy(tx+6,mac,6);w16(tx+12,kind);
}
static int frame(u32 end) {
    while ((long)(*ticks-end)<0) {
        if (!(nf_call(ETH(XIF_IRQ),0L)&1)) continue;
        rxlen=(int)nf_call(ETH(XIF_READLENGTH),0L);
        if(rxlen<0 || rxlen>(int)sizeof(rx))fail("invalid RX length");
        nf_call(ETH(XIF_READBLOCK),0L,rx,(long)rxlen);
        nf_call(ETH(XIF_IRQ),1L);
        if(rxlen>=14)return 1;
    }
    return 0;
}
static void arp(void) {
    static const u8 broadcast[6]={255,255,255,255,255,255};
    u32 end=*ticks+1000;
    ethernet(0x0806,broadcast);w16(tx+14,1);w16(tx+16,0x0800);
    tx[18]=6;tx[19]=4;w16(tx+20,1);copy(tx+22,mac,6);w32(tx+28,guest_ip);w32(tx+38,host_ip);
    send_frame(42);
    while(frame(end))if(rxlen>=42 && r16(rx+12)==0x0806 && r16(rx+20)==2 && r32(rx+28)==host_ip){copy(gateway,rx+22,6);return;}
    fail("ARP timeout");
}
static void ip(u32 source,u32 dest,int protocol,int n,const u8 *destmac) {
    ethernet(0x0800,destmac);tx[14]=0x45;w16(tx+16,(u16)(20+n));
    tx[22]=64;tx[23]=(u8)protocol;w32(tx+26,source);w32(tx+30,dest);
    w16(tx+24,finish(sum(tx+14,20)));
}
static void udp(u32 source,u32 dest,u16 sport,u16 dport,const u8 *body,int n) {
    static const u8 broadcast[6]={255,255,255,255,255,255};
    ip(source,dest,17,8+n,dest==0xffffffffUL?broadcast:gateway);
    w16(tx+34,sport);w16(tx+36,dport);w16(tx+38,(u16)(8+n));copy(tx+42,body,n);send_frame(42+n);
}
static u8 *udp_reply(u16 sport,u16 dport,u32 end,int *size) {
    while(frame(end)) {
        int ihl;
        if(rxlen<42 || r16(rx+12)!=0x0800 || rx[23]!=17)continue;
        ihl=(rx[14]&15)*4;
        if(ihl<20 || 14+ihl+8>rxlen)continue;
        if(r16(rx+14+ihl)!=sport || r16(rx+16+ihl)!=dport)continue;
        *size=r16(rx+18+ihl)-8;
        if(*size<0 || 14+ihl+8+*size>rxlen)continue;
        return rx+14+ihl+8;
    }
    return 0;
}
static int option(const u8 *p,int n,int key,const u8 *want,int size) {
    int i=240;
    while(i<n){int code=p[i++],len;if(code==255)break;if(code==0)continue;
        if(i>=n)break;len=p[i++];if(i+len>n)break;
        if(code==key)return len==size && equal(p+i,want,size);i+=len;}
    return 0;
}
static void dhcp(void) {
    u8 d[300],wanted[4],router[4],dns[4],type;int n;u8 *reply;u32 end;
    zero(d,sizeof(d));d[0]=1;d[1]=1;d[2]=6;w32(d+4,0x13572468UL);w16(d+10,0x8000);
    copy(d+28,mac,6);w32(d+236,0x63825363UL);d[240]=53;d[241]=1;d[242]=1;d[243]=255;
    udp(0,0xffffffffUL,68,67,d,sizeof(d));end=*ticks+2000;
    do {reply=udp_reply(67,68,end,&n);if(!reply)fail("DHCP offer timeout");type=2;}
    while(n<240 || r32(reply+4)!=0x13572468UL || !option(reply,n,53,&type,1));
    if(r32(reply+16)!=guest_ip)fail("DHCP offered wrong configured IP");
    d[242]=3;d[243]=50;d[244]=4;w32(d+245,guest_ip);d[249]=54;d[250]=4;w32(d+251,host_ip);d[255]=255;
    udp(0,0xffffffffUL,68,67,d,sizeof(d));end=*ticks+2000;
    do {reply=udp_reply(67,68,end,&n);if(!reply)fail("DHCP ACK timeout");type=5;}
    while(n<240 || r32(reply+4)!=0x13572468UL || !option(reply,n,53,&type,1));
    w32(wanted,guest_ip);w32(router,host_ip);w32(dns,host_ip+1);
    if(!equal(reply+16,wanted,4) || !option(reply,n,3,router,4) || !option(reply,n,6,dns,4))fail("DHCP configuration mismatch");
    report("SLIRP_GUEST PASS DHCP\r\n");
}
static void udp_echo(int cycle) {
    u8 body[19]="ARANYM SLIRP UDP ";int n;u8 *reply;u32 end;
    body[17]=(u8)('0'+cycle);udp(guest_ip,host_ip,40000,(u16)udp_port,body,18);
    end=*ticks+1000;
    do{reply=udp_reply((u16)udp_port,40000,end,&n);if(!reply)fail("UDP NAT timeout");}while(n!=18 || !equal(reply,body,18));
}
static void dns_test(void) {
    static const u8 query[]={0x4d,0x51,1,0,0,1,0,0,0,0,0,0,7,'e','x','a','m','p','l','e',3,'c','o','m',0,0,1,0,1};
    u8 *reply;int n;u32 end;
    udp(guest_ip,host_ip+1,40002,53,query,sizeof(query));end=*ticks+2000;
    do{reply=udp_reply(53,40002,end,&n);if(!reply)fail("DNS proxy timeout");}while(n<12 || r16(reply)!=0x4d51);
    if(!(reply[2]&0x80) || (reply[3]&15) || !r16(reply+6))fail("DNS answer invalid");
    report("SLIRP_GUEST PASS DNS\r\n");
}
static void tcp_send(u32 seq,u32 ack,u8 flags,u16 port,const u8 *data,int n) {
    u32 s;
    ip(guest_ip,host_ip,6,20+n,gateway);w16(tx+34,port);w16(tx+36,(u16)http_port);
    w32(tx+38,seq);w32(tx+42,ack);tx[46]=0x50;tx[47]=flags;w16(tx+48,4096);
    copy(tx+54,data,n);s=sum(tx+26,8)+6+20+n+sum(tx+34,20+n);w16(tx+50,finish(s));send_frame(54+n);
}
static u8 *tcp_reply(u16 port,u32 end,int *bytes) {
    while(frame(end)) {
        int ihl,total,th;
        if(rxlen<54 || r16(rx+12)!=0x0800 || rx[23]!=6)continue;
        ihl=(rx[14]&15)*4;total=r16(rx+16);
        if(ihl<20 || total+14>rxlen || ihl+20>total)continue;
        if(r16(rx+14+ihl)!=(u16)http_port || r16(rx+16+ihl)!=port)continue;
        th=(rx[14+ihl+12]>>4)*4;if(th<20 || ihl+th>total)continue;
        *bytes=total-ihl-th;return rx+14+ihl;
    }
    return 0;
}
static void http(int cycle) {
    char request[]="GET /aranym-slirp/0 HTTP/1.0\r\nHost: aranym-test\r\n\r\n";
    char expected[]="ARANYM SLIRP HTTP PASS 0";
    u8 result[600],*tcp;u16 port=(u16)(40100+cycle);u32 seq=0x12345000UL,ack,end;
    int n,used=0,found=0,i,req=length(request),th;
    request[18]=(char)('0'+cycle);
    expected[length(expected)-1]=(char)('0'+cycle);
    tcp_send(seq,0,2,port,(const u8 *)"",0);end=*ticks+1000;
    do{tcp=tcp_reply(port,end,&n);if(!tcp)fail("TCP SYN timeout");}while((tcp[13]&0x12)!=0x12 || r32(tcp+8)!=seq+1);
    ack=r32(tcp+4)+1;++seq;tcp_send(seq,ack,0x10,port,(const u8 *)"",0);
    tcp_send(seq,ack,0x18,port,(const u8 *)request,req);seq+=(u32)req;end=*ticks+1000;
    while((tcp=tcp_reply(port,end,&n))!=0) {
        if(tcp[13]&4)fail("TCP reset");
        if(r32(tcp+4)!=ack)continue;
        th=(tcp[12]>>4)*4;
        if(n){if(used+n>(int)sizeof(result))fail("HTTP response too large");copy(result+used,tcp+th,n);used+=n;ack+=(u32)n;}
        if(tcp[13]&1)++ack;
        if(n || (tcp[13]&1))tcp_send(seq,ack,0x10,port,(const u8 *)"",0);
        for(i=0;i+length(expected)<=used;i++)if(equal(result+i,(const u8 *)expected,length(expected)))found=1;
        if(found && (tcp[13]&1))break;
    }
    if(!found)fail("HTTP payload missing");
    tcp_send(seq,ack,0x14,port,(const u8 *)"",0);
}
static u32 parse_ip(const char *s) {
    u32 ipaddr=0,part=0;int i;
    for(i=0;i<4;i++){part=0;if(*s<'0'||*s>'9')fail("invalid NF IP");while(*s>='0'&&*s<='9')part=part*10+(u32)(*s++-'0');if(part>255)fail("invalid NF IP octet");ipaddr=(ipaddr<<8)|part;if(i<3 && *s++!='.')fail("invalid NF IP separator");}
    return ipaddr;
}
long guest_main(void) {
    char text[20];int i;u32 until;
    stderr_id=nf_id("NF_STDERR");shutdown_id=nf_id("NF_SHUTDOWN");nfEtherID=nf_id("ETHERNET");
    if(!stderr_id || !shutdown_id || !nfEtherID)for(;;){}
    if(nf_call(ETH(GET_VERSION))!=ARAETHER_NFAPI_VERSION || nf_call(ETH(XIF_INTLEVEL))!=3)fail("NF Ethernet API mismatch");
    if(!nf_call(ETH(XIF_GET_MAC),0L,mac,6L))fail("SLIRP device unavailable");
    nf_call(ETH(XIF_GET_IPHOST),0L,text,20L);host_ip=parse_ip(text);
    nf_call(ETH(XIF_GET_IPATARI),0L,text,20L);guest_ip=parse_ip(text);
    if(http_port>65535 || udp_port>65535)fail("fixture ports unpatched");
    /* Poll and acknowledge the real NF Ethernet API, with only level3 masked.
     * EmuTOS's higher-level timer interrupts keep its 200Hz clock running.
     */
    __asm__ volatile("move.w #0x2300,%%sr" : : : "memory","cc");
    for(i=0;i<3;i++) {
        if(nf_call(ETH(XIF_START),0L))fail("receiver start");
        if(!i)dhcp();arp();udp_echo(i);if(!i)dns_test();http(i);
        nf_call(ETH(XIF_STOP),0L);
    }
    report("SLIRP_GUEST PASS ARP UDP TCP HTTP restart\r\n");
    if(nf_call(ETH(XIF_START),0L))fail("idle receiver start");
    until=*ticks+40;while((long)(*ticks-until)<0){}
    report("SLIRP_GUEST PASS shutdown with active receiver\r\n");
    nf_call(shutdown_id,0L);return 1;
}
