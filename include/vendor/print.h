#ifndef PRINT_H
#define PRINT_H

#define ESC     "\x1b["
#define RED     ESC "31m"
#define GREEN   ESC "32m"
#define YELLOW  ESC "33m"
#define BLUE    ESC "34m"
#define BOLD    ESC "1m"
#define RESET   ESC "0m"

#define CLR_SCREEN() printf("\x1B[2J\x1B[H")

#endif