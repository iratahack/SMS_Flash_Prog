#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

/* Put printf format strings in program memory (flash) instead of RAM. */
#define printf(str, ...) printf_P(PSTR(str), ##__VA_ARGS__)

/* ANSI color helpers */
#define ESC "\x1b"
#define CSI "\x1b["

#define COLOR_RESET  CSI "0m"
#define COLOR_BRIGHT CSI "1m"
#define COLOR_DIM    CSI "2m"

/* Foreground */
#define FG_RED     CSI "31m"
#define FG_GREEN   CSI "32m"
#define FG_YELLOW  CSI "33m"
#define FG_BLUE    CSI "34m"
#define FG_MAGENTA CSI "35m"
#define FG_CYAN    CSI "36m"
#define FG_WHITE   CSI "37m"

/* Background */
#define BG_BLUE CSI "44m"
#define BG_CYAN CSI "46m"

/* Box drawing - Unicode box-drawing characters (stored in flash via PSTR) */
#define BOX_TL  PSTR("┌")
#define BOX_TR  PSTR("┐")
#define BOX_BL  PSTR("└")
#define BOX_BR  PSTR("┘")
#define BOX_H   PSTR("─")
#define BOX_V   PSTR("│")
#define BOX_TH  PSTR("┬")
#define BOX_BH  PSTR("┴")
#define BOX_LH  PSTR("├")
#define BOX_RH  PSTR("┤")
#define BOX_X   PSTR("┼")

#endif /* _COMMON_H_ */
