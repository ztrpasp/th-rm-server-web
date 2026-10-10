#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "commandline/cli.h"
#include "commandline/cmd_parser.h"
#include "tables/rm_acl_table.h"
#include "tables/rm_sid_table.h"
#include "tables/rm_tables.h"
#include "vendor/utils.h"

static char line[RM_MAX_LINE_LEN];
static int p;

static void info(const char *msg)
{
    print_to_cli(msg);
}

static void skip_spaces(void)
{
    while (line[p] == ' ' || line[p] == '\t') {
        p++;
    }
}

static const char *keyword(void)
{
    static char buf[RM_MAX_LINE_LEN + 1];
    int i = 0;

    while (isalnum((unsigned char)line[p]) || line[p] == '-' || line[p] == '_') {
        buf[i++] = line[p++];
    }
    buf[i] = '\0';
    return buf;
}

static bool streql(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static bool parse_dec_u32(uint32_t *out)
{
    const char *s = keyword();
    char *end = NULL;
    unsigned long v;

    if (!s || *s == '\0' || !isdigit((unsigned char)s[0])) {
        return false;
    }
    v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || v > 0xffffffffu) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_fixed(uint8_t *dst, int size)
{
    const char *s = keyword();
    size_t len;

    if (!s || !dst || size <= 0) {
        return false;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    len = strlen(s);
    if (len == 0 || (len % 2) != 0 || (int)(len / 2) > size) {
        return false;
    }
    memset(dst, 0, size);
    dst += size - (len / 2);
    for (size_t i = 0; i < len / 2; i++) {
        int hi = hex_value(s[i * 2]);
        int lo = hex_value(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        dst[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool parse_hex_u32(uint32_t *out)
{
    const char *s = keyword();
    size_t len;
    uint32_t result = 0;

    if (!s || !out) {
        return false;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    len = strlen(s);
    if (len == 0 || len > 8) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        int val = hex_value(s[i]);
        if (val < 0) {
            return false;
        }
        result = (result << 4) | val;
    }
    
    // ASID 范围检查: 实际系统中 AS ID 只用 0-255
    if (result > 0xFF) {
        cli_print("Error: ASID must be in range 0-255 (0x00 to 0xFF)\n");
        return false;
    }
    
    *out = result;
    return true;
}

static void basic_info_show_cli(void)
{
    cli_print("========== RM 基本配置 ==========\n");
    cli_print("  AID:           %d\n", my_as_info->aid);
    cli_print("  Security:      %d\n", my_as_info->security_level);
    cli_print("  Quick Mark:    0x%x\n", my_as_info->quick_mark);
    cli_print("=================================\n\n");
}

static void show_single_px_cli(void *px)
{
    if (!px) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%x ", *(uint16_t *)px);
    info(buf);
}

static void show_single_con_cli(void *con)
{
    if (!con) return;
    connect_info_t *ct = (connect_info_t *)con;
    
    cli_print("    BR: ");
    show_data(ct->br_pt->nid, 16, false);
    cli_print("\n");
    cli_print("        Port: %d\n", ct->br_pt->port);
    cli_print("        IP:   0x%x\n", ct->br_pt->ip_addr);
    cli_print("        PX:   ");
    rte_array_loop_function(ct->px_array, show_single_px_cli);
    cli_print("\n");
}

static void show_single_as_cli(void *as)
{
    if (!as) return;
    as_info_t *at = (as_info_t *)as;
    cli_print("  --- AS %d ---\n", at->aid);
    rte_array_loop_function(at->connect_array, show_single_con_cli);
}

static void as_store_show_cli(void)
{
    cli_print("========== AS 存储信息 ==========\n");
    rte_array_loop_function(as_store.as_array, show_single_as_cli);
    cli_print("=================================\n\n");
}

static void graph_show_cli(graph_t *tar)
{
    if (tar == NULL) {
        cli_print("AS Graph is NULL\n\n");
        return;
    }

    cli_print("========== AS 拓扑图 ==========\n");
    cli_print("  PX       AID1 -> AID2\n");
    cli_print("------------------------------\n");

    int i;
    graph_as_t *apt;
    graph_px_t *gpt;
    jsw_rbtrav_t *travel;

    for (i = 0; i < 256; i++) {
        apt = (tar->as_data)[i];
        if (apt == NULL) break;
        
        travel = jsw_rbtnew();
        gpt = (graph_px_t *)jsw_rbtfirst(travel, apt->px_data);
        for (;;) {
            if (gpt == NULL) break;
            cli_print("  0x%04x   %3d  ->  %3d\n", 
                gpt->px, apt->aid, gpt->apt->aid);
            gpt = (graph_px_t *)jsw_rbtnext(travel);
        }
        jsw_rbtdelete(travel);
    }

    cli_print("=================================\n\n");
}

static void parse_acl_add(void)
{
    rm_acl_rule_t rule;
    memset(&rule, 0, sizeof(rule));

    const char *s = keyword();
    if (!streql(s, "id")) {
        info("acl add syntax: acl add id <num> action allow|deny sid <hex> asid <hex> nid <hex>\r\n");
        return;
    }
    skip_spaces();
    if (!parse_dec_u32(&rule.id)) {
        info("Invalid acl id.\r\n");
        return;
    }

    skip_spaces();
    s = keyword();
    if (!streql(s, "action")) {
        info("Expect action.\r\n");
        return;
    }
    skip_spaces();
    s = keyword();
    if (streql(s, "allow")) {
        rule.action = RM_ACL_ALLOW;
    } else if (streql(s, "deny")) {
        rule.action = RM_ACL_DENY;
    } else {
        info("action must be allow or deny.\r\n");
        return;
    }

    skip_spaces();
    s = keyword();
    if (!streql(s, "sid")) {
        info("Expect sid.\r\n");
        return;
    }
    skip_spaces();
    if (!parse_hex_fixed(rule.sid, RM_ACL_SID_LEN)) {
        info("Invalid sid hex.\r\n");
        return;
    }

    skip_spaces();
    s = keyword();
    if (!streql(s, "asid")) {
        info("Expect asid.\r\n");
        return;
    }
    skip_spaces();
    if (!parse_hex_u32(&rule.asid)) {
        info("Invalid asid hex.\r\n");
        return;
    }

    skip_spaces();
    s = keyword();
    if (!streql(s, "nid")) {
        info("Expect nid.\r\n");
        return;
    }
    skip_spaces();
    if (!parse_hex_fixed(rule.nid, RM_ACL_NID_LEN)) {
        info("Invalid nid hex.\r\n");
        return;
    }

    if (rm_acl_rule_add(&rule) < 0) {
        info("Failed to add ACL rule, maybe duplicated id.\r\n");
        return;
    }

    info("ACL rule added.\r\n");
}

static void parse_acl_delete(void)
{
    const char *s = keyword();
    uint32_t id;

    if (!streql(s, "id")) {
        info("acl delete syntax: acl delete id <num>\r\n");
        return;
    }
    skip_spaces();
    if (!parse_dec_u32(&id)) {
        info("Invalid acl id.\r\n");
        return;
    }
    if (rm_acl_rule_delete(id) < 0) {
        info("ACL rule not found.\r\n");
        return;
    }
    info("ACL rule deleted.\r\n");
}

static void parse_acl(void)
{
    const char *s = keyword();
    skip_spaces();

    if (streql(s, "add")) {
        parse_acl_add();
    } else if (streql(s, "delete")) {
        parse_acl_delete();
    } else {
        info("Use: acl add|delete\r\n");
    }
}

static void parse_sid_list(void)
{
    int total = g_tsid_table.count;
    if (total == 0) {
        cli_print("SID 列表为空\r\n");
        return;
    }
    cli_print("========== SID 列表 (共 %d 个) ==========\r\n", total);
    for (int i = 0; i < total; i++) {
        cli_print("[%3d] ", i + 1);
        for (int j = 0; j < SID_LEN; j++) {
            cli_print("%02x", g_tsid_table.sid_list[i].tsid[j]);
        }
        cli_print("\r\n");
    }
    cli_print("=========================================\r\n");
}

static void parse_display(void)
{
    const char *s = keyword();

    if (streql(s, "") || streql(s, "config")) {
        basic_info_show_cli();
        as_store_show_cli();
    } else if (streql(s, "acl")) {
        rm_acl_rule_show_all();
    } else if (streql(s, "graph")) {
        graph_show_cli(my_as_info->as_graph);
    } else if (streql(s, "sid")) {
        parse_sid_list();
    } else if (streql(s, "status")) {
        status_show(my_as_info->my_status);
    } else {
        info("Unknown display target. Try: display, display acl, display config, display graph, display sid, display status\r\n");
    }
}

void parse_line(const char *s)
{
    strncpy(line, s, RM_MAX_LINE_LEN - 1);
    line[RM_MAX_LINE_LEN - 1] = '\0';
    p = 0;
    skip_spaces();

    const char *cmd = keyword();
    skip_spaces();

    if (streql(cmd, "help") || streql(cmd, "?")) {
        info(
            "  display [config|graph]\r\n"
            "      Display RM runtime information.\r\n"
            "  display acl\r\n"
            "      Display ACL rules.\r\n"
            "  display sid\r\n"
            "      Display SID list.\r\n"
            "  display status\r\n"
            "      Display packet and proxy counters.\r\n"
            "  acl add id <num> action allow|deny sid <hex> asid <hex> nid <hex>\r\n"
            "      Add an ACL rule.\r\n"
            "  acl delete id <num>\r\n"
            "      Delete an ACL rule.\r\n"
            "  help\r\n"
            "      Display available commands.\r\n"
            "  exit\r\n"
            "      Shutdown RM.\r\n"
        );
    } else if (streql(cmd, "display")) {
        parse_display();
    } else if (streql(cmd, "acl")) {
        parse_acl();
    } else if (streql(cmd, "exit")) {
        info("Bye...\r\n");
        kill(getpid(), SIGTERM);
    } else if (!streql(cmd, "")) {
        info("Unrecognized command. Type help for usage.\r\n");
    }
}
