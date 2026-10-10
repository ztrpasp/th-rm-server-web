#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <rte_lcore.h>

#include "commandline/cli.h"
#include "commandline/cmd_parser.h"

/* 基本按键定义 */
#define KEY_ENTER 13
#define KEY_NEWLINE 10
#define KEY_BACKSPACE_1 8
#define KEY_BACKSPACE_2 127
#define KEY_CTRL_A 1
#define KEY_CTRL_C 3
#define KEY_CTRL_D 4
#define KEY_CTRL_E 5
#define KEY_CTRL_K 11
#define KEY_CTRL_L 12
#define KEY_CTRL_U 21
#define KEY_ESC 27
#define KEY_TAB 9

/* CSI 序列第二字节 */
#define KEY_CSI '['
#define KEY_SS3 'O'

/* 方向键类型 (CSI/SS3 第三字节) */
#define KEY_UP 'A'
#define KEY_DOWN 'B'
#define KEY_RIGHT 'C'
#define KEY_LEFT 'D'
#define KEY_HOME 'H'
#define KEY_END 'F'

/* Delete/Home/End 键: ESC [ 数字 ~ */
#define KEY_DELETE_SUFFIX '~'

/* 命令历史配置 */
#define HISTORY_MAX 50

/* 接收缓冲区 - 支持光标移动 */
struct rx_buffer {
    char data[RM_CLI_RX_BUFFER_SIZE];
    int cursor;    /* 光标位置 (0-based) */
    int len;       /* 当前总行长度 */
};

/* 命令历史 */
static struct {
    char entries[HISTORY_MAX][RM_MAX_LINE_LEN];
    char saved[RM_MAX_LINE_LEN];
    int count;       /* 当前历史条数 */
    int current;     /* 当前浏览位置 */
    bool browsing;   /* 是否正在浏览历史 */
} cmd_history;

static struct rx_buffer rx_buffer;

/* 记录上一次光标位置，用于多行折行时的正确刷新 */
static int prev_cursor_pos = 0;

/* 获取终端宽度 */
static int get_terminal_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    return 80;
}

/* ========================================
   底层输出函数
   ======================================== */
static void writefd(const char *s)
{
    while (*s != '\0' && !atomic_load_explicit(&force_quit, memory_order_acquire)) {
        ssize_t n = write(1, s, strlen(s));
        if (n > 0) {
            s += n;
        } else if (errno != EAGAIN && errno != EINTR) {
            return;
        }
    }
}

static void writefd_char(char c)
{
    while (!atomic_load_explicit(&force_quit, memory_order_acquire)) {
        ssize_t n = write(1, &c, 1);
        if (n == 1) {
            return;
        } else if (errno != EAGAIN && errno != EINTR) {
            return;
        }
    }
}

static void write_wrapped_text(const char *s, int term_width, int *col)
{
    while (*s != '\0') {
        writefd_char(*s);
        (*col)++;
        if (*col >= term_width) {
            writefd("\r\n");
            *col = 0;
        }
        s++;
    }
}

void print_to_cli(const char *msg)
{
    writefd(msg);
}

void rm_cli_notify(const char *msg)
{
    print_to_cli(msg);
}

/* ========================================
   格式化输出函数（自动处理换行转换）
   ======================================== */
void cli_print(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    /* 将 \n 替换为 \r\n */
    char out[1024];
    int i, j = 0;
    for (i = 0; buf[i] && j < (int)sizeof(out) - 2; i++) {
        if (buf[i] == '\n') {
            out[j++] = '\r';
            out[j++] = '\n';
        } else {
            out[j++] = buf[i];
        }
    }
    out[j] = '\0';
    print_to_cli(out);
}

/* ========================================
   rx_buffer 操作函数
   ======================================== */
static void rx_buffer_reset(struct rx_buffer *rxb)
{
    rxb->cursor = 0;
    rxb->len = 0;
    prev_cursor_pos = 0;
}

/* 在光标位置插入字符 */
static bool rx_buffer_insert(struct rx_buffer *rxb, char c)
{
    if (rxb->len + 1 >= RM_CLI_RX_BUFFER_SIZE) {
        return false;
    }
    /* 光标后字符后移 */
    for (int i = rxb->len; i > rxb->cursor; i--) {
        rxb->data[i] = rxb->data[i - 1];
    }
    rxb->data[rxb->cursor] = c;
    rxb->cursor++;
    rxb->len++;
    return true;
}

/* 删除光标前一个字符 (Backspace) */
static void rx_buffer_backspace(struct rx_buffer *rxb)
{
    if (rxb->cursor == 0) {
        return;
    }
    for (int i = rxb->cursor - 1; i < rxb->len - 1; i++) {
        rxb->data[i] = rxb->data[i + 1];
    }
    rxb->cursor--;
    rxb->len--;
}

/* 删除光标处字符 (Delete) */
static void rx_buffer_delete(struct rx_buffer *rxb)
{
    if (rxb->cursor >= rxb->len) {
        return;
    }
    for (int i = rxb->cursor; i < rxb->len - 1; i++) {
        rxb->data[i] = rxb->data[i + 1];
    }
    rxb->len--;
}

/* 删除从光标到行尾 */
static void rx_buffer_kill_to_end(struct rx_buffer *rxb)
{
    rxb->len = rxb->cursor;
}

/* 删除整行 */
static void rx_buffer_clear(struct rx_buffer *rxb)
{
    rxb->cursor = 0;
    rxb->len = 0;
}

/* 光标左移 */
static void rx_buffer_left(struct rx_buffer *rxb)
{
    if (rxb->cursor > 0) {
        rxb->cursor--;
    }
}

/* 光标右移 */
static void rx_buffer_right(struct rx_buffer *rxb)
{
    if (rxb->cursor < rxb->len) {
        rxb->cursor++;
    }
}

/* 光标到行首 */
static void rx_buffer_home(struct rx_buffer *rxb)
{
    rxb->cursor = 0;
}

/* 光标到行尾 */
static void rx_buffer_end(struct rx_buffer *rxb)
{
    rxb->cursor = rxb->len;
}

static void rx_buffer_copystr(struct rx_buffer *rxb, char *dst)
{
    for (int i = 0; i < rxb->len; i++) {
        dst[i] = rxb->data[i];
    }
    dst[rxb->len] = '\0';
}

/* 用指定内容替换当前缓冲区 */
static void rx_buffer_replace(struct rx_buffer *rxb, const char *s)
{
    int n = strlen(s);
    if (n >= RM_CLI_RX_BUFFER_SIZE) {
        n = RM_CLI_RX_BUFFER_SIZE - 1;
    }
    for (int i = 0; i < n; i++) {
        rxb->data[i] = s[i];
    }
    rxb->len = n;
    rxb->cursor = n;
}

/* 刷新显示并定位光标 */
static void refresh_line(struct rx_buffer *rxb)
{
    char content[RM_CLI_RX_BUFFER_SIZE + 1];
    rx_buffer_copystr(rxb, content);

    int term_width = get_terminal_width();
    int prompt_len = 4; /* "rm> " */

    /* 从上次光标所在行回到上次渲染的第一行 */
    int prev_cursor_abs = prompt_len + prev_cursor_pos;
    int prev_cursor_row = prev_cursor_abs / term_width;
    for (int i = 0; i < prev_cursor_row; i++) {
        writefd("\x1b[A");
    }

    /* 清除旧内容 */
    writefd("\r");
    writefd("\x1b[0J");

    /* 手动折行渲染，避免依赖终端自动换行 */
    int col = 0;
    write_wrapped_text("rm> ", term_width, &col);
    write_wrapped_text(content, term_width, &col);

    prev_cursor_pos = rxb->cursor;

    /* 光标目标位置：手动折行模型下整除时在下一行第 0 列 */
    int target_pos = prompt_len + rxb->cursor;
    int render_total = prompt_len + rxb->len;
    int current_row = render_total / term_width;
    int target_row = target_pos / term_width;
    int target_col = target_pos % term_width;

    /* 渲染结束后光标在 current_row 行、当前列；先回到行首，再上移到目标行 */
    writefd("\r");
    for (int i = 0; i < current_row - target_row; i++) {
        writefd("\x1b[A");
    }

    if (target_col > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\x1b[%dC", target_col);
        writefd(buf);
    }
}

static void finish_current_line(struct rx_buffer *rxb)
{
    int term_width = get_terminal_width();
    int prompt_len = 4; /* "rm> " */
    int current_abs = prompt_len + prev_cursor_pos;
    int end_abs = prompt_len + rxb->len;
    int current_row = current_abs / term_width;
    int end_row = end_abs / term_width;
    int end_col = end_abs % term_width;

    writefd("\r");
    for (int i = current_row; i < end_row; i++) {
        writefd("\x1b[B");
    }
    if (end_col > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\x1b[%dC", end_col);
        writefd(buf);
        writefd("\r\n");
    } else {
        writefd("\r\n");
    }
}

/* ========================================
   命令历史操作
   ======================================== */
static void history_init(void)
{
    cmd_history.count = 0;
    cmd_history.current = 0;
    cmd_history.saved[0] = '\0';
    cmd_history.browsing = false;
}

static void history_add(const char *cmd)
{
    if (!cmd || strlen(cmd) == 0) {
        return;
    }
    /* 不重复添加相同的命令 */
    if (cmd_history.count > 0 && 
        strcmp(cmd_history.entries[cmd_history.count - 1], cmd) == 0) {
        return;
    }
    
    int idx = cmd_history.count;
    if (idx >= HISTORY_MAX) {
        /* 历史满了，往前挪一条 */
        for (int i = 1; i < HISTORY_MAX; i++) {
            strncpy(cmd_history.entries[i - 1], cmd_history.entries[i], RM_MAX_LINE_LEN - 1);
        }
        idx = HISTORY_MAX - 1;
    } else {
        cmd_history.count++;
    }
    strncpy(cmd_history.entries[idx], cmd, RM_MAX_LINE_LEN - 1);
    cmd_history.entries[idx][RM_MAX_LINE_LEN - 1] = '\0';
}

static const char *history_prev(const char *current_line)
{
    if (cmd_history.count == 0) {
        return NULL;
    }
    if (!cmd_history.browsing) {
        strncpy(cmd_history.saved, current_line ? current_line : "", RM_MAX_LINE_LEN - 1);
        cmd_history.saved[RM_MAX_LINE_LEN - 1] = '\0';
        cmd_history.current = cmd_history.count;
        cmd_history.browsing = true;
    }
    if (cmd_history.current > 0) {
        cmd_history.current--;
    }
    return cmd_history.entries[cmd_history.current];
}

static const char *history_next(void)
{
    if (!cmd_history.browsing) {
        return NULL;
    }
    if (cmd_history.current < cmd_history.count - 1) {
        cmd_history.current++;
        return cmd_history.entries[cmd_history.current];
    }
    cmd_history.current = cmd_history.count;
    cmd_history.browsing = false;
    return cmd_history.saved;
}

int cli_main_loop(__attribute__((unused)) void *dummy)
{
    struct termios opt;
    struct termios saved_opt;
    bool termios_saved = false;
    char buff[64];
    char line[RM_MAX_LINE_LEN];
    
    /* ESC 序列状态机 */
    enum {
        STATE_NORMAL,
        STATE_ESC,          /* 收到 ESC */
        STATE_CSI,          /* ESC [ */
        STATE_SS3,          /* ESC O (SS3) */
        STATE_CSI_NUM,      /* ESC [ 数字 */
    } state = STATE_NORMAL;
    int csi_num = 0;

    cli_print("[X] RM CLI is running on lcore %u\n", rte_lcore_id());

    if (tcgetattr(0, &opt) == 0) {
        saved_opt = opt;
        termios_saved = true;
        cfmakeraw(&opt);
        tcsetattr(0, TCSANOW, &opt);
    }
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    setbuf(stdout, NULL);
    writefd("\x1b[?25h"); /* 确保光标可见 */

    history_init();
    rx_buffer_reset(&rx_buffer);
    refresh_line(&rx_buffer);

    while (!atomic_load_explicit(&force_quit, memory_order_acquire)) {
        ssize_t nread = read(0, buff, sizeof(buff));
        if (nread <= 0) {
            usleep(10 * 1000);
            continue;
        }

        for (int i = 0; i < nread; i++) {
            char c = buff[i];
            
            switch (state) {
            /* ========================================
               正常状态
               ======================================== */
            case STATE_NORMAL:
                if (c == KEY_ESC) {
                    state = STATE_ESC;
                }
                else if (c == KEY_ENTER || c == KEY_NEWLINE) {
                    rx_buffer_copystr(&rx_buffer, line);
                    finish_current_line(&rx_buffer);
                    rx_buffer_reset(&rx_buffer);
                    cmd_history.current = cmd_history.count;
                    cmd_history.browsing = false;
                    if (strlen(line) > 0) {
                        history_add(line);
                        parse_line(line);
                    }
                    if (!atomic_load_explicit(&force_quit, memory_order_acquire)) {
                        refresh_line(&rx_buffer);
                    }
                }
                else if (c == KEY_BACKSPACE_1 || c == KEY_BACKSPACE_2) {
                    rx_buffer_backspace(&rx_buffer);
                    refresh_line(&rx_buffer);
                }
                else if (c == KEY_CTRL_A) {
                    rx_buffer_home(&rx_buffer);
                    refresh_line(&rx_buffer);
                }
                else if (c == KEY_CTRL_E) {
                    rx_buffer_end(&rx_buffer);
                    refresh_line(&rx_buffer);
                }
                else if (c == KEY_CTRL_K) {
                    rx_buffer_kill_to_end(&rx_buffer);
                    refresh_line(&rx_buffer);
                }
                else if (c == KEY_CTRL_U) {
                    rx_buffer_clear(&rx_buffer);
                    refresh_line(&rx_buffer);
                }
                else if (c == KEY_CTRL_L) {
                    writefd("\x1b[2J\x1b[H"); /* clear screen, cursor home */
                    prev_cursor_pos = 0;
                    refresh_line(&rx_buffer);
                }
                else if (c == KEY_CTRL_C) {
                    writefd("^C");
                    finish_current_line(&rx_buffer);
                    rx_buffer_reset(&rx_buffer);
                    cmd_history.browsing = false;
                    refresh_line(&rx_buffer);
                }
                else if (c == KEY_CTRL_D) {
                    if (rx_buffer.len == 0) {
                        /* 空行 Ctrl-D: 退出 */
                        kill(getpid(), SIGTERM);
                    } else {
                        rx_buffer_delete(&rx_buffer);
                        refresh_line(&rx_buffer);
                    }
                }
                else if (32 <= c && c <= 126) {
                    if (rx_buffer_insert(&rx_buffer, c)) {
                        refresh_line(&rx_buffer);
                    }
                }
                break;

            /* ========================================
               收到 ESC
               ======================================== */
            case STATE_ESC:
                if (c == KEY_CSI) {
                    state = STATE_CSI;
                } else if (c == KEY_SS3) {
                    state = STATE_SS3;
                } else {
                    state = STATE_NORMAL;
                }
                break;

            /* ========================================
               ESC [ 状态
               ======================================== */
            case STATE_CSI:
                switch (c) {
                case KEY_UP:
                    {
                        rx_buffer_copystr(&rx_buffer, line);
                        const char *prev = history_prev(line);
                        if (prev) {
                            rx_buffer_replace(&rx_buffer, prev);
                            refresh_line(&rx_buffer);
                        }
                    }
                    state = STATE_NORMAL;
                    break;
                case KEY_DOWN:
                    {
                        const char *next = history_next();
                        if (next) {
                            rx_buffer_replace(&rx_buffer, next);
                            refresh_line(&rx_buffer);
                        }
                    }
                    state = STATE_NORMAL;
                    break;
                case KEY_RIGHT:
                    rx_buffer_right(&rx_buffer);
                    refresh_line(&rx_buffer);
                    state = STATE_NORMAL;
                    break;
                case KEY_LEFT:
                    rx_buffer_left(&rx_buffer);
                    refresh_line(&rx_buffer);
                    state = STATE_NORMAL;
                    break;
                case KEY_HOME:
                    rx_buffer_home(&rx_buffer);
                    refresh_line(&rx_buffer);
                    state = STATE_NORMAL;
                    break;
                case KEY_END:
                    rx_buffer_end(&rx_buffer);
                    refresh_line(&rx_buffer);
                    state = STATE_NORMAL;
                    break;
                default:
                    if (c >= '0' && c <= '9') {
                        csi_num = c - '0';
                        state = STATE_CSI_NUM;
                    } else {
                        state = STATE_NORMAL;
                    }
                    break;
                }
                break;

            /* ========================================
               ESC O (SS3) 状态 — 某些终端的 Home/End
               ======================================== */
            case STATE_SS3:
                switch (c) {
                case KEY_HOME:
                    rx_buffer_home(&rx_buffer);
                    refresh_line(&rx_buffer);
                    break;
                case KEY_END:
                    rx_buffer_end(&rx_buffer);
                    refresh_line(&rx_buffer);
                    break;
                default:
                    break;
                }
                state = STATE_NORMAL;
                break;

            /* ========================================
               ESC [ 数字 状态，例如 Delete/Home/End
               ======================================== */
            case STATE_CSI_NUM:
                if (c >= '0' && c <= '9') {
                    csi_num = csi_num * 10 + (c - '0');
                } else if (c == KEY_DELETE_SUFFIX) {
                    switch (csi_num) {
                    case 1:
                    case 7:
                        rx_buffer_home(&rx_buffer);
                        refresh_line(&rx_buffer);
                        break;
                    case 3:
                        rx_buffer_delete(&rx_buffer);
                        refresh_line(&rx_buffer);
                        break;
                    case 4:
                    case 8:
                        rx_buffer_end(&rx_buffer);
                        refresh_line(&rx_buffer);
                        break;
                    default:
                        break;
                    }
                    state = STATE_NORMAL;
                } else {
                    state = STATE_NORMAL;
                }
                break;
            }
        }
    }

    writefd("\r\n");
    if (termios_saved) {
        tcsetattr(0, TCSANOW, &saved_opt);
    }
    return 0;
}
