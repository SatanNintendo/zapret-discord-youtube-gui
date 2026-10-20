/*
 * Zapret GUI — portsdlg.h
 * Modal dialog for the game filter TCP / UDP port ranges (zapret 1.10.4:
 * service.bat -> Game Filter -> options 5-7).
 */
#ifndef ZG_PORTSDLG_H
#define ZG_PORTSDLG_H

#include <windows.h>
#include "zapret.h"

/* edits gf->tcp / gf->udp; returns TRUE when the user saved valid ranges */
BOOL zg_ports_dialog(HWND owner, ZgGameFilter* gf);

#endif
