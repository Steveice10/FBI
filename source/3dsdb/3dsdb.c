#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "uthash.h"
#include "3dsdb.h"
#include "dblist.h"

map_element_t *game_map = NULL;
const char *NOTEXIST = "<N/A>";

int game_map_init() {
    int i;
    int n =  sizeof(gamelist) / sizeof(game_info_t);

    //init
    map_element_t *ret = NULL;
    for(i = 0; i < n; ++i) {
        uint64_t key = gamelist[i].titleid;
        HASH_FIND(hh, game_map, &key, sizeof(uint64_t), ret);
        if(ret != NULL) {
            continue;
        }
        map_element_t *e = (map_element_t *)malloc(sizeof(map_element_t));
        if(e == NULL) {
            continue;
        }
        e->key = key;
        e->info = &gamelist[i];
        HASH_ADD(hh, game_map, key, sizeof(uint64_t), e);
    }

    return HASH_COUNT(game_map);
}

void game_map_free() {
    map_element_t *tmp, *ret;
    HASH_ITER(hh, game_map, ret, tmp) {
      HASH_DEL(game_map, ret);
      free(ret);
    }
}

const char *game_map_search(uint64_t titleid) {
    map_element_t *ret;
    uint64_t key = titleid;
    HASH_FIND(hh, game_map, &key, sizeof(uint64_t), ret);
    if(ret == NULL) {
        return NOTEXIST;
    } else{
        return ret->info->name;
    }
}
