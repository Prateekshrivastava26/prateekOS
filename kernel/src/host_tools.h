#ifndef PRATEEKOS_HOST_TOOLS_H
#define PRATEEKOS_HOST_TOOLS_H

void host_tools_init(void);
int host_tools_poll(void);
int host_tools_available(void);
const char *host_tools_status(void);
int host_tools_open_wifi(void);
int host_tools_open_bluetooth(void);

#endif
