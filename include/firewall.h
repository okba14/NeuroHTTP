#ifndef AIONIC_FIREWALL_H
#define AIONIC_FIREWALL_H

#include "config.h"


typedef struct {
    char *ip_address;
    int request_count;
    time_t last_request;
    int is_blocked;
} FirewallEntry;


int firewall_init(const Config *config);
int firewall_check_request(const char *ip_address, const char *api_key);
int firewall_block_ip(const char *ip_address);
int firewall_unblock_ip(const char *ip_address);
int firewall_get_blocked_ips(char ***blocked_ips, int *count);
void firewall_cleanup();

#endif // AIONIC_FIREWALL_H
