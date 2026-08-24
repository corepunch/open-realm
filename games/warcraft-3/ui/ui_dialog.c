#include "ui_dialog.h"

static LPCSTR UI_DialogWar3IconPath(uiDialogWar3Icon_t icon) {
    switch (icon) {
        case UI_DIALOG_WAR3_ICON_ERROR:
            return "UI\\Widgets\\Glues\\dialogbox-error.blp";
        case UI_DIALOG_WAR3_ICON_QUESTION:
            return "UI\\Widgets\\Glues\\dialogbox-question.blp";
        case UI_DIALOG_WAR3_ICON_MESSAGE:
        default:
            return "UI\\Widgets\\Glues\\dialogbox-message.blp";
    }
}

static LPCSTR UI_DialogWar3TemplateName(uiDialogWar3Init_t const *init) {
    return init && init->template_name && init->template_name[0]
           ? init->template_name
           : "DialogWar3";
}

static LPCSTR UI_DialogWar3ModalName(uiDialogWar3Init_t const *init) {
    return init && init->modal_name && init->modal_name[0]
           ? init->modal_name
           : "DialogWar3Modal";
}

static void UI_DialogWar3SetVisible(uiDialogWar3_t *dialog, BOOL visible) {
    if (!dialog) {
        return;
    }
    UI_SetHidden(dialog->modal, !visible);
    UI_SetHidden(dialog->frame, !visible);
}

static BOOL UI_DialogWar3CreateModal(uiDialogWar3_t *dialog,
                                     LPFRAMEDEF parent,
                                     uiDialogWar3Init_t const *init)
{
    LPCSTR modal_name = UI_DialogWar3ModalName(init);

    dialog->modal = UI_FindFrame(modal_name);
    if (!dialog->modal) {
        dialog->modal = UI_Spawn(FT_DIALOG, NULL);
        if (!dialog->modal) {
            uiimport.Printf("ERROR: failed to create %s\n", modal_name);
            return false;
        }
        snprintf(dialog->modal->Name, sizeof(dialog->modal->Name), "%s", modal_name);
    } else {
        UI_SetParent(dialog->modal, NULL);
    }

    UI_SetPoint(dialog->modal, FRAMEPOINT_TOPLEFT, parent, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    UI_SetPoint(dialog->modal, FRAMEPOINT_BOTTOMRIGHT, parent, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);

    return true;
}

typedef struct {
    LPCSTR requested, authored, text;
} DIALOGTEMPLATE;
typedef DIALOGTEMPLATE *LPDIALOGTEMPLATE;
typedef const DIALOGTEMPLATE *LPCDIALOGTEMPLATE;

static const DIALOGTEMPLATE dialog_templates[] = {
    { "StandardDialogTemplate", "OpenWarcraftStandardDialog", "DialogText" },
    { "BattleNetDialogTemplate", "OpenWarcraftBattleNetDialog", "DialogText" },
    { "ScriptDialog", "OpenWarcraftScriptDialog", "ScriptDialogText" },
    { NULL, NULL, NULL },
};

/* Runtime names select a final project template; FDF remains the only source of its child geometry. */
static LPCDIALOGTEMPLATE UI_DialogWar3Template(LPCSTR name) {
    LPCDIALOGTEMPLATE it;
    for (it = dialog_templates; it->requested; it++)
        if (!strcmp(name, it->requested)) return it;
    return NULL;
}

/* The project templates compose children from all three Blizzard template families. */
static BOOL UI_DialogWar3EnsureTemplates(void) {
    return UI_EnsureFDF("UI\\FrameDef\\Glue\\StandardTemplates.fdf") &&
           UI_EnsureFDF("UI\\FrameDef\\Glue\\BattleNetTemplates.fdf") &&
           UI_EnsureFDF("UI\\FrameDef\\UI\\ScriptDialog.fdf") &&
           UI_EnsureFDF("UI\\FrameDef\\OpenWarcraft3\\DialogTemplates.fdf");
}

static LPFRAMEDEF UI_DialogWar3CloneTemplate(LPCSTR template_name, LPFRAMEDEF parent) {
    LPFRAMEDEF template_frame = UI_FindFrame(template_name);
    LPFRAMEDEF frame = template_frame ? UI_CloneFrameTree(template_frame, parent) : NULL;
    if (frame) {
        frame->Parent = parent;
    }
    return frame;
}

static void UI_DialogWar3BindCommon(uiDialogWar3_t *dialog) {
    dialog->frame = dialog->frames.DialogWar3;
    dialog->text = dialog->frames.DialogText;
    dialog->icon = dialog->frames.DialogIcon;
    dialog->ok_backdrop = dialog->frames.DialogButtonOKBackdrop;
    dialog->ok_button = dialog->frames.DialogButtonOK;
    dialog->no_backdrop = dialog->frames.DialogButtonNoBackdrop;
    dialog->no_button = dialog->frames.DialogButtonNo;
    dialog->yes_backdrop = dialog->frames.DialogButtonYesBackdrop;
    dialog->yes_button = dialog->frames.DialogButtonYes;
}

static BOOL UI_DialogWar3CreateLegacy(uiDialogWar3_t *dialog) {
    if (!DialogWar3_Load(&dialog->frames)) {
        return false;
    }
    UI_SetParent(dialog->frames.DialogWar3, dialog->modal);
    UI_SetPoint(dialog->frames.DialogWar3, FRAMEPOINT_CENTER, dialog->modal, FRAMEPOINT_CENTER, 0.0f, 0.0f);
    UI_DialogWar3BindCommon(dialog);
    dialog->default_height = dialog->frame->Height;
    return true;
}

static BOOL UI_DialogWar3CreateTemplate(uiDialogWar3_t *dialog,
                                        uiDialogWar3Init_t const *init)
{
    LPCSTR template_name = UI_DialogWar3TemplateName(init);
    LPCDIALOGTEMPLATE layout;

    if (!strcmp(template_name, "DialogWar3")) {
        return UI_DialogWar3CreateLegacy(dialog);
    }
    layout = UI_DialogWar3Template(template_name);
    if (!layout || !UI_DialogWar3EnsureTemplates()) {
        uiimport.Printf("ERROR: unsupported dialog template: %s\n", template_name);
        return false;
    }
    dialog->frame = UI_DialogWar3CloneTemplate(layout->authored, dialog->modal);
    if (!dialog->frame) {
        return false;
    }
    UI_SetPoint(dialog->frame, FRAMEPOINT_CENTER, dialog->modal, FRAMEPOINT_CENTER, 0.0f, 0.0f);
    dialog->text = UI_FindChildFrame(dialog->frame, layout->text);
    dialog->ok_button = UI_FindChildFrame(dialog->frame, "DialogButtonOK");
    dialog->ok_backdrop = dialog->ok_button;
    dialog->frame->DialogBackdrop = UI_FindChildFrame(dialog->frame, dialog->frame->DialogBackdropName);
    if (!dialog->text || !dialog->ok_button) {
        uiimport.Printf("ERROR: authored dialog %s is missing required children\n", layout->authored);
        return false;
    }
    dialog->default_height = dialog->frame->Height;

    return true;
}

BOOL UI_DialogWar3Init(uiDialogWar3_t *dialog,
                       LPFRAMEDEF parent,
                       uiDialogWar3Init_t const *init)
{
    if (!dialog || !parent) {
        return false;
    }

    memset(dialog, 0, sizeof(*dialog));
    dialog->parent = parent;

    if (!UI_DialogWar3CreateModal(dialog, parent, init)) {
        return false;
    }
    if (!UI_DialogWar3CreateTemplate(dialog, init)) {
        UI_DialogWar3SetVisible(dialog, false);
        return false;
    }

    UI_DialogWar3SetVisible(dialog, false);
    return true;
}

void UI_DialogWar3Show(uiDialogWar3_t *dialog,
                       uiDialogWar3Config_t const *config)
{
    if (!dialog || !dialog->frame || !config) {
        return;
    }

    if (dialog->text) {
        UI_SetText(dialog->text, "%s", config->message ? config->message : "");
    }
    if (dialog->icon) {
        dialog->icon->Backdrop.Background = UI_LoadTexture(UI_DialogWar3IconPath(config->icon), false);
    }

    UI_SetHidden(dialog->ok_backdrop, config->buttons != UI_DIALOG_WAR3_BUTTONS_OK);
    UI_SetHidden(dialog->no_backdrop, config->buttons != UI_DIALOG_WAR3_BUTTONS_YES_NO);
    UI_SetHidden(dialog->yes_backdrop, config->buttons != UI_DIALOG_WAR3_BUTTONS_YES_NO);

    UI_SetOnClick(dialog->ok_button, "%s", config->ok_command ? config->ok_command : "");
    UI_SetOnClick(dialog->no_button, "%s", config->no_command ? config->no_command : "");
    UI_SetOnClick(dialog->yes_button, "%s", config->yes_command ? config->yes_command : "");

    UI_DialogWar3SetVisible(dialog, true);
}

void UI_DialogWar3Hide(uiDialogWar3_t *dialog) {
    UI_DialogWar3SetVisible(dialog, false);
}

BOOL UI_DialogWar3Visible(uiDialogWar3_t const *dialog) {
    return dialog && dialog->modal && dialog->frame &&
           !dialog->modal->hidden && !dialog->frame->hidden;
}
