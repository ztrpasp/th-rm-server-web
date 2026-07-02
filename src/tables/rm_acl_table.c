#include "tables/rm_acl_table.h"

#include <stdio.h>
#include <string.h>

#include "tables/rm_tables.h"
#include "vendor/print.h"
#include "commandline/cli.h"

static void print_sid_hex(const uint8_t *sid)
{
    for (int i = 0; i < RM_ACL_SID_LEN; i++) {
        cli_print("%02x", sid[i]);
    }
}

static void print_nid_hex(const uint8_t *nid)
{
    for (int i = 0; i < RM_ACL_NID_LEN; i++) {
        cli_print("%02x", nid[i]);
    }
}

static int acl_rule_index_by_id(uint32_t id)
{
    if (!acl_store.rule_array) {
        return -1;
    }
    for (int i = 0; i < acl_store.rule_array->count; i++) {
        rm_acl_rule_t *rule = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, i);
        if (rule && rule->id == id) {
            return i;
        }
    }
    return -1;
}

int rm_acl_table_init(void)
{
    if (acl_store.rule_array) {
        rte_array_destroy(acl_store.rule_array);
    }
    acl_store.rule_array = rte_array_create(16, sizeof(rm_acl_rule_t));
    return acl_store.rule_array ? 0 : -1;
}

bool rm_acl_sid_nid_match(const rm_acl_rule_t *rule, const uint8_t *sid, uint32_t asid, const uint8_t *nid)
{
    if (!rule || !sid || !nid) {
        return false;
    }
    
    // 检查 nid 是否全为 0（通配符，匹配任意 nid）
    bool nid_wildcard = true;
    for (int i = 0; i < RM_ACL_NID_LEN; i++) {
        if (rule->nid[i] != 0) {
            nid_wildcard = false;
            break;
        }
    }
    
    // SID 和 ASID 必须精确匹配
    if (memcmp(rule->sid, sid, RM_ACL_SID_LEN) != 0 || rule->asid != asid) {
        return false;
    }
    
    // nid 通配或精确匹配
    if (nid_wildcard) {
        return true;
    }
    return memcmp(rule->nid, nid, RM_ACL_NID_LEN) == 0;
}

int rm_acl_rule_add(const rm_acl_rule_t *rule)
{
    if (!rule || !acl_store.rule_array) {
        return -1;
    }
    if (acl_rule_index_by_id(rule->id) >= 0) {
        return -2;
    }

    int insert_pos = acl_store.rule_array->count;
    for (int i = 0; i < acl_store.rule_array->count; i++) {
        rm_acl_rule_t *cur = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, i);
        if (cur && rule->id < cur->id) {
            insert_pos = i;
            break;
        }
    }

    rm_acl_rule_t blank = {0};
    if (rte_array_add(acl_store.rule_array, &blank) < 0) {
        return -1;
    }

    for (int i = acl_store.rule_array->count - 1; i > insert_pos; i--) {
        rm_acl_rule_t *dst = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, i);
        rm_acl_rule_t *src = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, i - 1);
        if (dst && src) {
            *dst = *src;
        }
    }

    rm_acl_rule_t *slot = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, insert_pos);
    if (!slot) {
        return -1;
    }
    *slot = *rule;
    return 0;
}

int rm_acl_rule_delete(uint32_t id)
{
    int idx = acl_rule_index_by_id(id);
    if (idx < 0) {
        return -1;
    }
    return rte_array_delete(acl_store.rule_array, idx);
}

int rm_acl_rule_find(uint32_t id, rm_acl_rule_t *out)
{
    int idx = acl_rule_index_by_id(id);
    if (idx < 0) {
        return -1;
    }
    if (out) {
        rm_acl_rule_t *rule = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, idx);
        *out = *rule;
    }
    return 0;
}

void rm_acl_rule_show_all(void)
{
    if (!acl_store.rule_array || acl_store.rule_array->count == 0) {
        cli_print("ACL table is empty\n");
        return;
    }

    for (int i = 0; i < acl_store.rule_array->count; i++) {
        rm_acl_rule_t *rule = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, i);
        if (!rule) {
            continue;
        }
        // 检查 nid 是否为通配符
        bool nid_wildcard = true;
        for (int j = 0; j < RM_ACL_NID_LEN; j++) {
            if (rule->nid[j] != 0) {
                nid_wildcard = false;
                break;
            }
        }
        cli_print("id=%u action=%s sid=0x", rule->id, rule->action == RM_ACL_ALLOW ? "allow" : "deny");
        print_sid_hex(rule->sid);
        cli_print(" asid=0x%08x nid=0x", rule->asid);
        print_nid_hex(rule->nid);
        if (nid_wildcard) {
            cli_print(" (通配符: 匹配任意nid)");
        }
        cli_print("\n");
    }
}

int rm_acl_check_get(const uint8_t *sid, uint32_t asid, const uint8_t *nid, uint32_t *matched_id)
{
    if (!acl_store.rule_array || !sid || !nid) {
        return 1;
    }

    for (int i = 0; i < acl_store.rule_array->count; i++) {
        rm_acl_rule_t *rule = (rm_acl_rule_t *)rte_array_get(acl_store.rule_array, i);
        if (!rule) {
            continue;
        }
        if (rm_acl_sid_nid_match(rule, sid, asid, nid)) {
            if (matched_id) {
                *matched_id = rule->id;
            }
            return rule->action == RM_ACL_ALLOW ? 1 : 0;
        }
    }
    return 1;
}
