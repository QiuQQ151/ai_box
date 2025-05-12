#pragma once

void sntp_init_task(void);
void sntp_update_time(void);
void time_sync_notification_cb(struct timeval *tv);
void print_local_time(void);



