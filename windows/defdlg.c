/*
 * Default dialog procedure
 *
 * Copyright 1993, 1996 Alexandre Julliard
 *
 */

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "wine/winuser16.h"
#include "controls.h"
#include "win.h"
#include "winproc.h"


/***********************************************************************
 *           DEFDLG_SetFocus
 *
 * Set the focus to a control of the dialog, selecting the text if
 * the control is an edit dialog.
 */
static void DEFDLG_SetFocus( HWND hwndDlg, HWND hwndCtrl )
{
    HWND hwndPrev = GetFocus();

    if (IsChild( hwndDlg, hwndPrev ))
    {
        if (SendMessageW( hwndPrev, WM_GETDLGCODE, 0, 0 ) & DLGC_HASSETSEL)
            SendMessageW( hwndPrev, EM_SETSEL, -1, 0 );
    }
    if (SendMessageW( hwndCtrl, WM_GETDLGCODE, 0, 0 ) & DLGC_HASSETSEL)
        SendMessageW( hwndCtrl, EM_SETSEL, 0, -1 );
    SetFocus( hwndCtrl );
}


/***********************************************************************
 *           DEFDLG_SaveFocus
 */
static void DEFDLG_SaveFocus( HWND hwnd )
{
    DIALOGINFO *infoPtr;
    WND *wndPtr;
    HWND hwndFocus = GetFocus();

    if (!hwndFocus || !IsChild( hwnd, hwndFocus )) return;
    if (!(wndPtr = WIN_FindWndPtr( hwnd ))) return;
    infoPtr = (DIALOGINFO *)&wndPtr->wExtra;
    infoPtr->hwndFocus = hwndFocus;
    WIN_ReleaseWndPtr( wndPtr );
    /* Remove default button */
}


/***********************************************************************
 *           DEFDLG_RestoreFocus
 */
static void DEFDLG_RestoreFocus( HWND hwnd )
{
    DIALOGINFO *infoPtr;
    WND *wndPtr;

    if (IsIconic( hwnd )) return;
    if (!(wndPtr = WIN_FindWndPtr( hwnd ))) return;
    infoPtr = (DIALOGINFO *)&wndPtr->wExtra;
    if (IsWindow( infoPtr->hwndFocus ))
    {
        /* Don't set the focus back to controls if EndDialog is already called.*/
        if (!(infoPtr->flags & DF_END))
        {
            WIN_ReleaseWndPtr( wndPtr );
            DEFDLG_SetFocus( hwnd, infoPtr->hwndFocus );
            return;
        }
        /* This used to set infoPtr->hwndFocus to NULL for no apparent reason,
           sometimes losing focus when receiving WM_SETFOCUS messages. */
    }
    WIN_ReleaseWndPtr( wndPtr );
}


/***********************************************************************
 *           DEFDLG_FindDefButton
 *
 * Find the current default push-button.
 */
static HWND DEFDLG_FindDefButton( HWND hwndDlg )
{
    HWND hwndChild = GetWindow( hwndDlg, GW_CHILD );
    while (hwndChild)
    {
        if (SendMessageW( hwndChild, WM_GETDLGCODE, 0, 0 ) & DLGC_DEFPUSHBUTTON)
            break;
        hwndChild = GetWindow( hwndChild, GW_HWNDNEXT );
    }
    return hwndChild;
}


/***********************************************************************
 *           DEFDLG_SetDefButton
 *
 * Set the new default button to be hwndNew.
 */
static BOOL DEFDLG_SetDefButton( HWND hwndDlg, DIALOGINFO *dlgInfo,
                                   HWND hwndNew )
{
    if (hwndNew &&
        !(SendMessageW(hwndNew, WM_GETDLGCODE, 0, 0 ) & DLGC_UNDEFPUSHBUTTON))
        return FALSE;  /* Destination is not a push button */
    
    if (dlgInfo->idResult)  /* There's already a default pushbutton */
    {
        HWND hwndOld = GetDlgItem( hwndDlg, dlgInfo->idResult );
        if (SendMessageA( hwndOld, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON)
            SendMessageA( hwndOld, BM_SETSTYLE, BS_PUSHBUTTON, TRUE );
    }
    if (hwndNew)
    {
        SendMessageA( hwndNew, BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE );
        dlgInfo->idResult = GetDlgCtrlID( hwndNew );
    }
    else dlgInfo->idResult = 0;
    return TRUE;
}


/***********************************************************************
 *           DEFDLG_Proc
 *
 * Implementation of DefDlgProc(). Only handle messages that need special
 * handling for dialogs.
 */
static LRESULT DEFDLG_Proc( HWND hwnd, UINT msg, WPARAM wParam,
                            LPARAM lParam, DIALOGINFO *dlgInfo )
{
    switch(msg)
    {
        case WM_ERASEBKGND:
	    FillWindow16( hwnd, hwnd, (HDC16)wParam, (HBRUSH16)CTLCOLOR_DLG );
	    return 1;

	case WM_NCDESTROY:

	      /* Free dialog heap (if created) */
	    if (dlgInfo->hDialogHeap)
	    {
		GlobalUnlock16(dlgInfo->hDialogHeap);
		GlobalFree16(dlgInfo->hDialogHeap);
		dlgInfo->hDialogHeap = 0;
	    }

	      /* Delete font */
	    if (dlgInfo->hUserFont)
	    {
		DeleteObject( dlgInfo->hUserFont );
		dlgInfo->hUserFont = 0;
	    }

	      /* Delete menu */
	    if (dlgInfo->hMenu)
	    {		
		DestroyMenu( dlgInfo->hMenu );
		dlgInfo->hMenu = 0;
	    }

            /* Delete window procedure */
            WINPROC_FreeProc( dlgInfo->dlgProc, WIN_PROC_WINDOW );
            dlgInfo->dlgProc = (HWINDOWPROC)0;
            dlgInfo->flags |= DF_END;  /* just in case */

	      /* Window clean-up */
	    return DefWindowProcA( hwnd, msg, wParam, lParam );

	case WM_SHOWWINDOW:
	    if (!wParam) DEFDLG_SaveFocus( hwnd );
	    return DefWindowProcA( hwnd, msg, wParam, lParam );

	case WM_ACTIVATE:
	    if (wParam) DEFDLG_RestoreFocus( hwnd );
	    else DEFDLG_SaveFocus( hwnd );
	    return 0;

	case WM_SETFOCUS:
	    DEFDLG_RestoreFocus( hwnd );
	    return 0;

        case DM_SETDEFID:
            if (dlgInfo->flags & DF_END) return 1;
            DEFDLG_SetDefButton( hwnd, dlgInfo,
                                 wParam ? GetDlgItem( hwnd, wParam ) : 0 );
            return 1;

        case DM_GETDEFID:
            {
                HWND hwndDefId;
                if (dlgInfo->flags & DF_END) return 0;
                if (dlgInfo->idResult)
                    return MAKELONG( dlgInfo->idResult, DC_HASDEFID );
                if ((hwndDefId = DEFDLG_FindDefButton( hwnd )))
                    return MAKELONG( GetDlgCtrlID( hwndDefId ), DC_HASDEFID);
            }
	    return 0;

	case WM_NEXTDLGCTL:
	    {
                HWND hwndDest = (HWND)wParam;
                if (!lParam)
                    hwndDest = GetNextDlgTabItem(hwnd, GetFocus(), wParam);
                if (hwndDest) DEFDLG_SetFocus( hwnd, hwndDest );
                DEFDLG_SetDefButton( hwnd, dlgInfo, hwndDest );
            }
            return 0;

        case WM_ENTERMENULOOP:
        case WM_LBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
            {
                HWND hwndFocus = GetFocus();
                if (hwndFocus)
                {
                    /* always make combo box hide its listbox control */
                    if (!SendMessageA( hwndFocus, CB_SHOWDROPDOWN, FALSE, 0 ))
                        SendMessageA( GetParent(hwndFocus), CB_SHOWDROPDOWN, FALSE, 0 );
                }
            }
	    return DefWindowProcA( hwnd, msg, wParam, lParam );

	case WM_GETFONT: 
	    return dlgInfo->hUserFont;

        case WM_CLOSE:
            PostMessageA( hwnd, WM_COMMAND, IDCANCEL,
                            (LPARAM)GetDlgItem( hwnd, IDCANCEL ) );
            return 0;
    
        case WM_NOTIFYFORMAT:
	    return DefWindowProcA( hwnd, msg, wParam, lParam );
    }
    return 0;
}

/***********************************************************************
 *           DEFDLG_Epilog
 */
static LRESULT DEFDLG_Epilog(HWND hwnd, UINT msg, BOOL fResult)
{
    /* see SDK 3.1 */

    if ((msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC) ||
	 msg == WM_CTLCOLOR || msg == WM_COMPAREITEM ||
         msg == WM_VKEYTOITEM || msg == WM_CHARTOITEM ||
         msg == WM_QUERYDRAGICON || msg == WM_INITDIALOG)
        return fResult; 

    return GetWindowLongA( hwnd, DWL_MSGRESULT );
}

/***********************************************************************
 *		DefDlgProc (USER.308)
 */
LRESULT WINAPI DefDlgProc16( HWND16 hwnd, UINT16 msg, WPARAM16 wParam,
                             LPARAM lParam )
{
    DIALOGINFO * dlgInfo;
    WNDPROC16 dlgproc;
    BOOL result = FALSE;
    WND * wndPtr = WIN_FindWndPtr( hwnd );

    if (!wndPtr) return 0;
    dlgInfo = (DIALOGINFO *)&wndPtr->wExtra;
    dlgInfo->msgResult = 0;
    dlgproc = (WNDPROC16)dlgInfo->dlgProc;
    WIN_ReleaseWndPtr(wndPtr);

    if (dlgproc)
    {
        /* Call dialog procedure */
        result = CallWindowProc16( dlgproc, hwnd, msg, wParam, lParam );
        /* 16 bit dlg procs only return BOOL16 */
        if( WINPROC_GetProcType( dlgproc ) == WIN_PROC_16 )
            result = LOWORD(result);
    }

    if (!result && IsWindow(hwnd))
    {
        /* callback didn't process this message */

        switch(msg)
        {
            case WM_ERASEBKGND:
            case WM_SHOWWINDOW:
            case WM_ACTIVATE:
            case WM_SETFOCUS:
            case DM_SETDEFID:
            case DM_GETDEFID:
            case WM_NEXTDLGCTL:
            case WM_GETFONT:
            case WM_CLOSE:
            case WM_NCDESTROY:
            case WM_ENTERMENULOOP:
            case WM_LBUTTONDOWN:
            case WM_NCLBUTTONDOWN:
                return DEFDLG_Proc( (HWND)hwnd, msg, 
                                    (WPARAM)wParam, lParam, dlgInfo );
            case WM_INITDIALOG:
            case WM_VKEYTOITEM:
            case WM_COMPAREITEM:
            case WM_CHARTOITEM:
                break;

            default:
                return DefWindowProc16( hwnd, msg, wParam, lParam );
        }
    }
    return DEFDLG_Epilog(hwnd, msg, result);
}


/***********************************************************************
 *		DefDlgProcA (USER32.@)
 */
LRESULT WINAPI DefDlgProcA( HWND hwnd, UINT msg,
                              WPARAM wParam, LPARAM lParam )
{
    DIALOGINFO * dlgInfo;
    WNDPROC dlgproc;
    BOOL result = FALSE;
    WND * wndPtr = WIN_FindWndPtr( hwnd );

    if (!wndPtr) return 0;
    dlgInfo = (DIALOGINFO *)&wndPtr->wExtra;
    dlgInfo->msgResult = 0;
    dlgproc = dlgInfo->dlgProc;
    WIN_ReleaseWndPtr(wndPtr);

    if (dlgproc)
    {
        /* Call dialog procedure */
        result = CallWindowProcA( dlgproc, hwnd, msg, wParam, lParam );
        /* 16 bit dlg procs only return BOOL16 */
        if( WINPROC_GetProcType( dlgproc ) == WIN_PROC_16 )
            result = LOWORD(result);
    }

    if (!result && IsWindow(hwnd))
    {
        /* callback didn't process this message */

        switch(msg)
        {
            case WM_ERASEBKGND:
            case WM_SHOWWINDOW:
            case WM_ACTIVATE:
            case WM_SETFOCUS:
            case DM_SETDEFID:
            case DM_GETDEFID:
            case WM_NEXTDLGCTL:
            case WM_GETFONT:
            case WM_CLOSE:
            case WM_NCDESTROY:
            case WM_ENTERMENULOOP:
            case WM_LBUTTONDOWN:
            case WM_NCLBUTTONDOWN:
                 return DEFDLG_Proc( hwnd, msg, wParam, lParam, dlgInfo );
            case WM_INITDIALOG:
            case WM_VKEYTOITEM:
            case WM_COMPAREITEM:
            case WM_CHARTOITEM:
                 break;

            default:
                 return DefWindowProcA( hwnd, msg, wParam, lParam );
        }
    }
    return DEFDLG_Epilog(hwnd, msg, result);
}


/***********************************************************************
 *		DefDlgProcW (USER32.@)
 */
LRESULT WINAPI DefDlgProcW( HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam )
{
    DIALOGINFO * dlgInfo;
    BOOL result = FALSE;
    WNDPROC dlgproc;
    WND * wndPtr = WIN_FindWndPtr( hwnd );

    if (!wndPtr) return 0;
    dlgInfo = (DIALOGINFO *)&wndPtr->wExtra;
    dlgInfo->msgResult = 0;
    dlgproc = dlgInfo->dlgProc;
    WIN_ReleaseWndPtr(wndPtr);

    if (dlgproc)
    {
        /* Call dialog procedure */
        result = CallWindowProcW( dlgproc, hwnd, msg, wParam, lParam );
        /* 16 bit dlg procs only return BOOL16 */
        if( WINPROC_GetProcType( dlgproc ) == WIN_PROC_16 )
            result = LOWORD(result);
    }

    if (!result && IsWindow(hwnd))
    {
        /* callback didn't process this message */

        switch(msg)
        {
            case WM_ERASEBKGND:
            case WM_SHOWWINDOW:
            case WM_ACTIVATE:
            case WM_SETFOCUS:
            case DM_SETDEFID:
            case DM_GETDEFID:
            case WM_NEXTDLGCTL:
            case WM_GETFONT:
            case WM_CLOSE:
            case WM_NCDESTROY:
            case WM_ENTERMENULOOP:
            case WM_LBUTTONDOWN:
            case WM_NCLBUTTONDOWN:
                 return DEFDLG_Proc( hwnd, msg, wParam, lParam, dlgInfo );
            case WM_INITDIALOG:
            case WM_VKEYTOITEM:
            case WM_COMPAREITEM:
            case WM_CHARTOITEM:
                 break;

            default:
                 return DefWindowProcW( hwnd, msg, wParam, lParam );
        }
    }
    return DEFDLG_Epilog(hwnd, msg, result);
}
