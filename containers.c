/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>

#include "containers.h"
#include "util.h"

int list_item_count(listItself list)
{
    void **l = (void**)list;
    int i = 0;
    while(l && l[i])
        ++i;
    return i;
}

int list_size(listItself list)
{
    return list_item_count(list)+1;
}

void list_add(void *item, ptrToList list_p)
{
    void ***list = (void***)list_p;

    int i = 0;
    while(*list && (*list)[i])
        ++i;
    i += 2; // NULL and the new item

    *list = realloc(*list, i*sizeof(item));

    (*list)[--i] = NULL;
    (*list)[--i] = item;
}

int list_add_from_list(listItself src_p, ptrToList list_p)
{
    void **src = (void**)src_p;
    void ***list = (void***)list_p;
    int i, len_src = 0, len_list = 0;

    while(src && src[len_src])
        ++len_src;

    if(len_src == 0)
        return 0;

    while(*list && (*list)[len_list])
        ++len_list;

    ++len_src; // for NULL
    *list = realloc(*list, (len_list+len_src)*sizeof(void*));

    for(i = 0; i < len_src; ++i)
        (*list)[i+len_list] = src[i];
    return len_src-1;
}

int list_rm_opt(int reorder, void *item, ptrToList list_p, callback destroy_callback_p)
{
    void ***list = (void***)list_p;
    callbackPtr destroy_callback = (callbackPtr)destroy_callback_p;

    int size = list_size(*list);

    int i;
    for(i = 0; *list && (*list)[i]; ++i)
    {
        if((*list)[i] != item)
            continue;

        if(destroy_callback)
            (*destroy_callback)(item);

        --size;
        if(size == 1)
        {
            free(*list);
            *list = NULL;
            return 0;
        }

        if(i != size-1)
        {
            if(reorder)
                (*list)[i] = (*list)[size-1];
            else
            {
                for(; *list && (*list)[i]; ++i)
                    (*list)[i] = (*list)[i+1];
            }
        }

        *list= realloc(*list, size*sizeof(item));
        (*list)[size-1] = NULL;
        return 0;
    }
    return -1;
}

int list_rm(void *item, ptrToList list_p, callback destroy_callback_p)
{
    return list_rm_opt(1, item, list_p, destroy_callback_p);
}

int list_rm_noreorder(void *item, ptrToList list_p, callback destroy_callback_p)
{
    return list_rm_opt(0, item, list_p, destroy_callback_p);
}

int list_rm_at(int idx, ptrToList list_p, callback destroy_callback_p)
{
    void ***list = (void***)list_p;
    callbackPtr destroy_callback = (callbackPtr)destroy_callback_p;

    int size = list_size(*list);
    if(idx < 0 || idx >= size-1)
        return -1;

    void *item = (*list)[idx];
    if(destroy_callback)
        (*destroy_callback)(item);

    --size;
    if(size == 1)
    {
        free(*list);
        *list = NULL;
        return 0;
    }

    int i = idx;
    for(; i < size; ++i)
        (*list)[i] = (*list)[i+1];

    *list= realloc(*list, size*sizeof(item));
    return 0;
}

void list_clear(ptrToList list_p, callback destroy_callback_p)
{
    void ***list = (void***)list_p;
    callbackPtr destroy_callback = (callbackPtr)destroy_callback_p;

    if(*list == NULL)
        return;

    if(destroy_callback)
    {
        int i;
        for(i = 0; *list && (*list)[i]; ++i)
            (*destroy_callback)((*list)[i]);
    }

    free(*list);
    *list = NULL;
}

int list_copy(listItself src, ptrToList dest_p)
{
    void **source = (void**)src;
    void ***dest = (void***)dest_p;

    if(!source)
        return 0;

    if(*dest)
        return -1;

    int size = list_size(source);
    *dest = calloc(size, sizeof(*source));

    int i;
    for(i = 0; source[i]; ++i)
        (*dest)[i] = source[i];
    return 0;
}

int list_move(ptrToList source_p, ptrToList dest_p)
{
    void ***source = (void***)source_p;
    void ***dest = (void***)dest_p;

    if(!source)
        return 0;

    if(*dest)
        return -1;

    *dest = *source;
    *source = NULL;
    return 0;
}

void list_swap(ptrToList a_p, ptrToList b_p)
{
    void ***a = (void***)a_p;
    void ***b = (void***)b_p;
    void **tmp = *a;
    *a = *b;
    *b = tmp;
}

map *map_create(void)
{
    map *m = mzalloc(sizeof(map));
    return m;
}

void map_destroy(map *m, void (*destroy_callback)(void*))
{
    if(!m)
        return;

    list_clear(&m->keys, &free);
    list_clear(&m->values, destroy_callback);
    free(m);
}

void map_add(map *m, char *key, void *val, void (*destroy_callback)(void*))
{
    int idx = map_find(m, key);
    if(idx >= 0)
    {
        if(destroy_callback)
            (*destroy_callback)(m->values[idx]);
        m->values[idx] = val;
    }
    else
        map_add_not_exist(m, key, val);
}

void map_add_not_exist(map *m, char *key, void *val)
{
    list_add(strdup(key), &m->keys);
    list_add(val, &m->values);
}

void map_rm(map *m, char *key, void (*destroy_callback)(void*))
{
    int idx = map_find(m, key);
    if(idx < 0)
        return;

    list_rm_at(idx, &m->keys, &free);
    list_rm_at(idx, &m->values, destroy_callback);
}

int map_find(map *m, char *key)
{
    int i;
    for(i = 0; m->keys && m->keys[i]; ++i)
        if(strcmp(m->keys[i], key) == 0)
            return i;
    return -1; 
}

void *map_get_val(map *m, char *key)
{
    int idx = map_find(m, key);
    if(idx < 0)
        return NULL;
    return m->values[idx];
}

void *map_get_ref(map *m, char *key)
{
    int idx = map_find(m, key);
    if(idx < 0)
        return NULL;
    return &m->values[idx];
}
