#ifndef __RM_STATUS_H__
#define __RM_STATUS_H__

#include <stdint.h>
#include <stdio.h>

#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_malloc.h>
#include <rte_mempool.h>


typedef struct status_ANN
{
    // basic info
    uint64_t from_inside_count;
    uint64_t from_outside_count;
    uint64_t outbound_count;
    uint64_t filtered_count;

    // trace as
    uint64_t trace_res[256];

} status_ANN_t;

typedef struct status_proxy
{
    uint64_t register_count;
} status_proxy_t;

typedef struct status_GET
{
    // basic info
    uint64_t from_inside_count;
    uint64_t from_outside_count;
    uint64_t to_inside_count;
    uint64_t to_outside_count;
    uint64_t filtered_count;

    // trace as
    uint64_t trace_res[256];

} status_GET_t;

typedef struct rm_status
{
    status_ANN_t ANN_stat;
    status_GET_t GET_stat;
    status_proxy_t proxy_stat;
} rm_status_t;

#define ANN_STAT_TYPE 0
#define GET_STAT_TYPE 1
#define PROXY_STAT_TYPE 2

// function
rm_status_t *status_create();
void status_destroy(rm_status_t *st);
int status_snapshot(const rm_status_t *st, rm_status_t *out);
int status_construct(const rm_status_t *st, void *buf, uint8_t type);
void status_show(const rm_status_t *st);

#endif
