#ifndef __3DSDB_H_
#define __3DSDB_H_
#include <stdint.h>
#include "uthash.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t titleid;
    const char* name;
} game_info_t;

typedef struct {  
    uint64_t key;
    game_info_t *info; 
    UT_hash_handle hh;
} map_element_t;


int game_map_init();
void game_map_free();

const char *game_map_search(uint64_t titleid);

#ifdef __cplusplus
}
#endif


#endif