local LOGIN_SCREEN = {
    account_name = "",
    visible = false,
}

local LOGIN_BG     = "Interface\\Glues\\GlueScreens\\GlueXML\\Login-Background.blp"
local DIALOG_BG    = "Interface\\DialogFrame\\UI-DialogBox-Background.blp"
local DIALOG_EDGE  = "Interface\\DialogFrame\\UI-DialogBox-Border.blp"
local BUTTON_UP    = "Interface\\Buttons\\UI-DialogBox-Button-Up.blp"
local BUTTON_DOWN  = "Interface\\Buttons\\UI-DialogBox-Button-Down.blp"

function ow3_show_login()
    LOGIN_SCREEN.visible = true
    LOGIN_SCREEN.account_name = ""
end

function ow3_hide_login()
    LOGIN_SCREEN.visible = false
end

function ow3_login_proceed()
    ow3.command("menu_character_select")
end

function ow3_login_handle_text(text)
    if text == "\r" or text == "\n" then
        ow3_login_proceed()
    elseif text == "\b" then
        if #LOGIN_SCREEN.account_name > 0 then
            LOGIN_SCREEN.account_name = LOGIN_SCREEN.account_name:sub(1, -2)
        end
    elseif #text == 1 and text >= " " and text <= "~" then
        if #LOGIN_SCREEN.account_name < 32 then
            LOGIN_SCREEN.account_name = LOGIN_SCREEN.account_name .. text
        end
    end
end

local function draw_panel(bg, border, x, y, w, h, bsz)
    ow3.draw_backdrop(bg, border, x, y, w, h, bsz or 0.004)
end

local function draw_button(label, x, y, w, h, hover)
    local tex = hover and BUTTON_DOWN or BUTTON_UP
    ow3.draw_image(tex, x, y, w, h)
    ow3.draw_text(label, x, y, w, h, 14, 255, 255, 255, 255, 'center')
end

function ow3_draw_login_screen()
    if not LOGIN_SCREEN.visible then
        return
    end

    -- Full-screen glue background (3D world vista texture in real WoW)
    ow3.draw_image(LOGIN_BG, 0, 0, 1, 1)

    -- Dialog panel (centred, ~40% wide)
    local px, py, pw, ph = 0.30, 0.28, 0.40, 0.40
    draw_panel(DIALOG_BG, DIALOG_EDGE, px, py, pw, ph, 0.005)

    -- Title
    ow3.draw_text('World of Warcraft', px, py + 0.02, pw, 0.06, 24,
                  255, 215, 120, 255, 'center')

    -- Account name label
    ow3.draw_text('Account Name:', px + 0.03, py + 0.12, pw - 0.06, 0.04, 14,
                  220, 220, 220, 255, 'left')

    -- Text field backdrop
    local fx, fy, fw, fh = px + 0.03, py + 0.17, pw - 0.06, 0.05
    draw_panel(DIALOG_BG, DIALOG_EDGE, fx, fy, fw, fh, 0.003)
    ow3.draw_text(LOGIN_SCREEN.account_name, fx + 0.01, fy, fw - 0.02, fh, 14,
                  255, 255, 255, 255, 'left')

    -- Blinking cursor
    if math.floor((ow3.time() / 500) % 2) == 0 then
        local cx = fx + 0.01 + #LOGIN_SCREEN.account_name * 0.008
        ow3.draw_text('|', cx, fy, 0.01, fh, 14, 255, 255, 255, 200, 'left')
    end

    -- Login button
    local bx, by, bw, bh = px + 0.12, py + 0.30, 0.16, 0.052
    draw_button('LOG IN', bx, by, bw, bh, false)
end
