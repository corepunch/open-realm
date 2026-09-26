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
    cstring_t modal_name;
    cstring_t template_name;
} uiDialogWar3Init_t;

typedef struct {
    cstring_t message;
    uiDialogWar3Icon_t icon;
    uiDialogWar3Buttons_t buttons;
    cstring_t ok_command;
    cstring_t yes_command;
    cstring_t no_command;
} uiDialogWar3Config_t;

typedef struct {
    frameDef_t *parent;
    frameDef_t *modal;
    frameDef_t *frame;
    frameDef_t *text;
    frameDef_t *icon;
    frameDef_t *ok_backdrop;
    frameDef_t *ok_button;
    frameDef_t *no_backdrop;
    frameDef_t *no_button;
    frameDef_t *yes_backdrop;
    frameDef_t *yes_button;
    DialogWar3_t frames;
    float default_height;
} uiDialogWar3_t;

bool UI_DialogWar3Init(uiDialogWar3_t *dialog,
                       frameDef_t *parent,
                       uiDialogWar3Init_t const *init);
void UI_DialogWar3Show(uiDialogWar3_t *dialog,
                       uiDialogWar3Config_t const *config);
void UI_DialogWar3Hide(uiDialogWar3_t *dialog);
bool UI_DialogWar3Visible(uiDialogWar3_t const *dialog);

#endif /* MENU_DIALOG_H */
