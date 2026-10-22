#include "rtc.h"
#include <stddef.h>

/* Read the PC/QEMU CMOS clock without enabling an RTC interrupt. */
static inline uint8_t rtc_inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void rtc_outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static uint8_t cmos_read(uint8_t index) {
    rtc_outb(0x70, index & 0x7F); /* Keep NMI enabled. */
    return rtc_inb(0x71);
}

typedef struct {
    uint8_t second, minute, hour, day, month, year, century, mode;
} rtc_raw_t;

static int read_snapshot(rtc_raw_t *r) {
    if (cmos_read(0x0A) & 0x80) return 0; /* Update in progress. */
    r->second=cmos_read(0); r->minute=cmos_read(2); r->hour=cmos_read(4);
    r->day=cmos_read(7); r->month=cmos_read(8); r->year=cmos_read(9);
    r->century=cmos_read(0x32); /* QEMU's PC century register. */
    r->mode=cmos_read(0x0B);
    if (cmos_read(0x0A) & 0x80) return 0;
    return cmos_read(0)==r->second;
}

static int bcd_value(uint8_t v) {
    if ((v & 15)>9 || (v >> 4)>9) return -1;
    return (v >> 4)*10+(v & 15);
}

static int rtc_decode(const rtc_raw_t *r, rtc_datetime_t *out) {
    int binary=(r->mode & 4)!=0;
    int sec=binary?r->second:bcd_value(r->second);
    int min=binary?r->minute:bcd_value(r->minute);
    int hour=binary?(r->hour & 0x7F):bcd_value(r->hour & 0x7F);
    int day=binary?r->day:bcd_value(r->day);
    int month=binary?r->month:bcd_value(r->month);
    int year=binary?r->year:bcd_value(r->year);
    int century=binary?r->century:bcd_value(r->century);
    if (!(r->mode & 2)) {
        if (hour<1 || hour>12) return 0;
        hour=hour%12+((r->hour & 0x80)?12:0);
    }
    if(sec<0 || sec>59 || min<0 || min>59 || hour<0 || hour>23 ||
       day<1 || month<1 || month>12 || year<0 || year>99) return 0;
    if(century<19 || century>99) century=20;
    year+=century*100;
    static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int leap=year%4==0 && (year%100!=0 || year%400==0);
    int max_day=days[month-1]+(month==2 && leap);
    if(day>max_day || year<1970) return 0;
    *out=(rtc_datetime_t){(uint16_t)year,(uint8_t)month,(uint8_t)day,
                         (uint8_t)hour,(uint8_t)min,(uint8_t)sec};
    return 1;
}

int rtc_read_time(rtc_datetime_t *out) {
    if(out==NULL) return 0;
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    int valid=0;
    for(unsigned i=0;i<4;i++) {
        rtc_raw_t a,b;
        if(!read_snapshot(&a) || !read_snapshot(&b)) continue;
        if(a.second!=b.second || a.minute!=b.minute || a.hour!=b.hour ||
           a.day!=b.day || a.month!=b.month || a.year!=b.year ||
           a.century!=b.century || a.mode!=b.mode) continue;
        valid=rtc_decode(&a,out);
        break;
    }
    if(flags & (1ULL<<9)) __asm__ volatile ("sti" : : : "memory");
    return valid;
}

void rtc_format_time(const rtc_datetime_t *t, char out[9]) {
    out[0]='0'+t->hour/10; out[1]='0'+t->hour%10; out[2]=':';
    out[3]='0'+t->minute/10; out[4]='0'+t->minute%10; out[5]=':';
    out[6]='0'+t->second/10; out[7]='0'+t->second%10; out[8]='\0';
}
void rtc_format_date(const rtc_datetime_t *t, char out[11]) {
    out[0]='0'+t->year/1000%10; out[1]='0'+t->year/100%10;
    out[2]='0'+t->year/10%10; out[3]='0'+t->year%10; out[4]='-';
    out[5]='0'+t->month/10; out[6]='0'+t->month%10; out[7]='-';
    out[8]='0'+t->day/10; out[9]='0'+t->day%10; out[10]='\0';
}
