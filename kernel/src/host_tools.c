#include "host_tools.h"
#include <stddef.h>
#include <stdint.h>

/* COM2 is connected only to the local launcher helper, never to a shell. */
#define HOST_UART 0x2F8
static int present, available, pending;
static char line[96];
static size_t used;
static int dropping;
static const char *status="Host tools unavailable. Start with make run.";

static inline uint8_t port_in(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void port_out(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static int equal(const char *a, const char *b) {
    while(*a && *a==*b) { a++; b++; }
    return *a==*b;
}
static int send_line(const char *s) {
    if(!present) return 0;
    for(size_t i=0;s[i];i++) {
        unsigned wait=0;
        while(!(port_in(HOST_UART+5)&0x20)) {
            if(++wait>=10000) return 0;
        }
        port_out(HOST_UART,(uint8_t)s[i]);
    }
    return 1;
}
static void accept_line(void) {
    if(equal(line,"PRATEEKOS/1 READY WINDOWS")) {
        available=1;pending=0;status="Windows connection tools are ready.";
    } else if(equal(line,"PRATEEKOS/1 READY UNAVAILABLE")) {
        available=0;pending=0;status="Windows tools unavailable on this host.";
    } else if(equal(line,"PRATEEKOS/1 OK WIFI")) {
        pending=0;status="Windows Wi-Fi settings opened. Switch to Windows.";
    } else if(equal(line,"PRATEEKOS/1 OK BLUETOOTH")) {
        pending=0;status="Windows Bluetooth settings opened. Switch to Windows.";
    } else if(equal(line,"PRATEEKOS/1 ERROR OPEN")) {
        pending=0;status="Windows settings could not open. Use Windows Settings.";
    } else if(equal(line,"PRATEEKOS/1 ERROR BUSY")) {
        pending=0;status="Please wait a moment before opening settings again.";
    }
}
void host_tools_init(void) {
    available=0;pending=0;used=0;dropping=0;present=0;
    status="Host tools unavailable. Start with make run.";
    if(port_in(HOST_UART+5)==0xFF) return;
    port_out(HOST_UART+1,0); /* Poll, no UART interrupt. */
    port_out(HOST_UART+3,0x80);
    port_out(HOST_UART,1); port_out(HOST_UART+1,0); /* 115200 baud. */
    port_out(HOST_UART+3,3); port_out(HOST_UART+2,0xC7);
    port_out(HOST_UART+4,0x1E); /* UART loopback presence check. */
    port_out(HOST_UART,0xAE);
    unsigned wait=0;
    while(!(port_in(HOST_UART+5)&1) && wait++<10000) {}
    uint8_t probe=port_in(HOST_UART);
    port_out(HOST_UART+4,0x0B);
    if(probe!=0xAE) return;
    present=1;
    status="Waiting for Windows tools. Use the updated launcher.";
    (void)send_line("PRATEEKOS/1 PING\n");
}
int host_tools_poll(void) {
    if(!present) return 0;
    const char *before=status;
    for(unsigned i=0;i<64;i++) {
        uint8_t state=port_in(HOST_UART+5);
        if(state==0xFF || !(state&1)) break;
        uint8_t ch=port_in(HOST_UART);
        if(state&0x1E) { used=0;dropping=1;continue; }
        if(ch=='\r') continue;
        if(ch=='\n') {
            if(!dropping) { line[used]='\0';accept_line(); }
            used=0;dropping=0;
        } else if(ch<32 || ch>126 || used+1>=sizeof(line)) {
            used=0;dropping=1;
        } else if(!dropping) line[used++]=(char)ch;
    }
    return before!=status;
}
int host_tools_available(void) { return available; }
const char *host_tools_status(void) { return status; }
static int request(const char *command) {
    if(!available || pending) return 0;
    if(!send_line(command)) {
        available=0;status="Host tool connection lost. Restart with make run.";return 0;
    }
    pending=1;status="Opening Windows settings...";return 1;
}
int host_tools_open_wifi(void) { return request("PRATEEKOS/1 WIFI\n"); }
int host_tools_open_bluetooth(void) { return request("PRATEEKOS/1 BLUETOOTH\n"); }
