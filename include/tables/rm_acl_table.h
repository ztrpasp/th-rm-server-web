#ifndef __RM_ACL_TABLE_H__
#define __RM_ACL_TABLE_H__

#include <stdint.h>
#include <stdbool.h>

#include "vendor/rte_array.h"

#define RM_ACL_SID_LEN 36
#define RM_ACL_NID_LEN 16

typedef enum rm_acl_action
{
    RM_ACL_DENY = 0,
    RM_ACL_ALLOW = 1,
} rm_acl_action_t;

typedef struct rm_acl_rule
{
    uint32_t id;
    rm_acl_action_t action;
    uint8_t sid[RM_ACL_SID_LEN];
    uint32_t asid;
    uint8_t nid[RM_ACL_NID_LEN];
} rm_acl_rule_t;

typedef struct rm_acl_store
{
    rte_array_t *rule_array;
} rm_acl_store_t;

int rm_acl_table_init(void);
int rm_acl_rule_add(const rm_acl_rule_t *rule);
int rm_acl_rule_delete(uint32_t id);
int rm_acl_rule_find(uint32_t id, rm_acl_rule_t *out);
void rm_acl_rule_show_all(void);
int rm_acl_check_get(const uint8_t *sid, uint32_t asid, const uint8_t *nid, uint32_t *matched_id);
bool rm_acl_sid_nid_match(const rm_acl_rule_t *rule, const uint8_t *sid, uint32_t asid, const uint8_t *nid);

#endif
