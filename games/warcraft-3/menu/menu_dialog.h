#ifndef MENU_DIALOG_H
#define MENU_DIALOG_H

#include "menu_local.h"
#include "generated/dialog_war3.h"

typedef enum {
    UI_DIALOG_WAR3_ICON_MESSAGE,
    UI_DIALOG_WAR3_ICON_ERROR,
    UI_DIALOG_WAR3_ICON_QUESTION,
} uiDialogWar3Icon_t;

typedef enum {
    UI_DIALOG_WAR3_BUTTONS_OK,
    UI_DIALOG_WAR3_BUTTONS_YES_NO,
} uiDialogWar3Buttons_t;

typedef struct {
    LPCSTR modal_name;
    LPCSTR template_name;
} uiDialogWar3Init_t;

typedef struct {
    LPCSTR message;
    uiDialogWar3Icon_t icon;
    uiDialogWar3Buttons_t buttons;
    LPCSTR ok_command;
    LPCSTR yes_command;
    LPCSTR no_command;
} uiDialogWar3Config_t;

typedef struct {
    LPFRAMEDEF parent;
    LPFRAMEDEF modal;
    LPFRAMEDEF frame;
    LPFRAMEDEF text;
    LPFRAMEDEF icon;
    LPFRAMEDEF ok_backdrop;
    LPFRAMEDEF ok_button;
    LPFRAMEDEF no_backdrop;
    LPFRAMEDEF no_button;
    LPFRAMEDEF yes_backdrop;
    LPFRAMEDEF yes_button;
    DialogWar3_t frames;
    FLOAT default_height;
} uiDialogWar3_t;

BOOL UI_DialogWar3Init(uiDialogWar3_t *dialog,
                       LPFRAMEDEF parent,
                       uiDialogWar3Init_t const *init);
void UI_DialogWar3Show(uiDialogWar3_t *dialog,
                       uiDialogWar3Config_t const *config);
void UI_DialogWar3Hide(uiDialogWar3_t *dialog);
BOOL UI_DialogWar3Visible(uiDialogWar3_t const *dialog);

#endif /* MENU_DIALOG_H */
