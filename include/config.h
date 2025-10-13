#ifndef AIONIC_CONFIG_H
#define AIONIC_CONFIG_H


typedef struct {
    int port;                
    int thread_count;        
    int max_connections;     
    int request_timeout;     
    int buffer_size;         
    char *log_file;         
    char *api_keys[64];     
    int api_key_count;       
    int enable_cache;        
    int cache_size;          
    int cache_ttl;           
    int enable_firewall;      
    int enable_optimization;  
} Config;


int load_config(const char *filename, Config *config);
void free_config(Config *config);

#endif // AIONIC_CONFIG_H
