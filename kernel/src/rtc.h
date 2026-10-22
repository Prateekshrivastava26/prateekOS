#ifndef PRATEEKOS_RTC_H
#define PRATEEKOS_RTC_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} rtc_datetime_t;

int rtc_read_time(rtc_datetime_t *out);
void rtc_format_time(const rtc_datetime_t *time, char out[9]);
void rtc_format_date(const rtc_datetime_t *time, char out[11]);

#endif
