/*****************************************************************************
** File:
**      EditControls.h
**
** Copyright (C) 2003-2004 Daniel Vik
**
** Modified 2026 by Hesoten for blueMSX+ fork.
** See https://github.com/Hesoten/blueMSX-plus for change history.
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
******************************************************************************
*/
#ifndef EDIT_CONTROLS_H
#define EDIT_CONTROLS_H

#include "SymbolInfo.h"
#include <windows.h>
#include <string>
#include <list>
#include <map>

class CpuRegisters;

class InputDialog {
public:
    enum { EC_NEWVALUE = WM_USER + 7029, EC_KILLFOCUS = WM_USER + 7030,
           EC_NAVIGATE = WM_USER + 7031 };

    /* EC_NAVIGATE lParam: where the owning view should take the edit cursor.
    ** NAV_DONE commits and leaves edit mode, NAV_CANCEL discards. NAV_NEXT /
    ** NAV_PREV are Tab / Shift+Tab -- the view's own idea of the next item. */
    enum { NAV_DONE = 1, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT,
           NAV_CANCEL, NAV_NEXT, NAV_PREV };

    InputDialog(HWND parent, int x, int y, int width, int height);
    ~InputDialog();

    void setPosition(int x, int y);
    void setSize(int width, int height);
    void setFont(HFONT font);
    void show();
    void hide();
    void setFocus();

    /* An overlay box must cover exactly `chars` cells of the monospaced output
    ** text; the extra half / quarter cell is the border and caret inset. */
    static int boxWidth(int chars, int textWidth) { return chars * textWidth + textWidth / 2; }
    static int boxHeight(int textHeight)          { return textHeight + textHeight / 4; }

    bool isVisible() const { return IsWindowVisible(hwnd) != 0; }

    /* Pastes the clipboard into the focused box, false if there is none so the
    ** caller can treat the key as its own shortcut instead. */
    static bool pasteToFocused();

    /* False until the box is typed in or its text differs from the seed, so
    ** leaving a box alone never writes the displayed value back. The seed
    ** covers paste and delete, the count covers retyping the same text. */
    bool isModified();

protected:
    HWND hwnd;

    /* Text the box was last seeded with, for isModified(). */
    char seedText[64];

    /* Keystrokes accepted since the seed. The overlay boxes also use it to
    ** tell when `chars` of them have arrived. */
    int charCount;

    void resetModified();

    /* Replaces the whole text and leaves it selected, the way the overlay
    ** boxes always show their value. */
    void setBoxText(const char* text);

    /* Puts the seed text back and marks the box unedited: what is left once
    ** the last typed character has been taken away again. */
    void restoreSeed();

    /* Only the in-place overlay boxes navigate; the modal Goto / Find dialogs
    ** keep their plain edit behaviour. */
    bool navEnabled;

    void initDialog();

    /* Turn arrows / Tab / Enter into EC_NAVIGATE for the owning view. Left and
    ** right only move on once the caret sits at that end. */
    int navigateKey(int keyCode);
    
    virtual BOOL dlgProc(UINT iMsg, WPARAM wParam, LPARAM lParam) = 0;

private:
    int wx;
    int wy;
    int wwidth;
    int wheight;
    HWND pparent;

    static std::map<HWND, InputDialog*> dialogMap;
    static int  richeditVersion;

    /* The richedit of the box the caret is in, or NULL if it is elsewhere. */
    static HWND focusedEdit();

    void initControl(HWND thisHwnd);

public:
    /* Entry point for the edit-control subclass, which only knows the HWND. */
    static int navigateFrom(HWND dlg, int keyCode);

private:

    static void applyDarkCharFormat(HWND hEdit);
    static void initRichEditControlDll();
    static INT_PTR CALLBACK dlgStaticProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);
};


class HexInputDialog : public InputDialog {
public:
    HexInputDialog(HWND parent, int x, int y, int width, int height, int numChars, 
                   bool returnNeeded = false, SymbolInfo* symInfo = NULL,
                   CpuRegisters* cpuRegs = NULL);
    ~HexInputDialog();

    void setValue(int value, bool setFocus = true);
    int  getValue();
    bool hasValue();

protected:
    virtual BOOL dlgProc(UINT iMsg, WPARAM wParam, LPARAM lParam);

private:
    /* Shows fastValue right aligned in `chars` hex digits. */
    void setBoxValue();

    int chars;
    bool needReturn;
    int  fastValue;
    SymbolInfo* symbolInfo;
    CpuRegisters* cpuRegisters;
};



class TextInputDialog : public InputDialog {
public:
    TextInputDialog(HWND parent, int x, int y, int width, int height, int numChars, bool returnNeeded = false);
    ~TextInputDialog();

    void setValue(const char* value, bool setFocus = true);
    const char* getValue();

protected:
    virtual BOOL dlgProc(UINT iMsg, WPARAM wParam, LPARAM lParam);
    
private:
    int chars;
    char text[512];
    bool needReturn;
};


#endif //EDIT_CONTROLS_H
