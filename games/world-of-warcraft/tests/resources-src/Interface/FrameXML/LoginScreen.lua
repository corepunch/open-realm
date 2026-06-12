-- Textures from Interface/GlueXML/AccountLogin.xml
local BACKGROUND    = "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp"
local LOGO          = "Interface\\Glues\\Common\\Glues-WoW-Logo.blp"
local EDITBOX_BG    = "Interface\\Tooltips\\UI-Tooltip-Background.blp"
local EDITBOX_EDGE  = "Interface\\Glues\\Common\\Glue-Tooltip-Border.blp"
local BUTTON_UP     = "Interface\\Glues\\Common\\Glues-BigButton-Up.blp"
local BUTTON_DOWN   = "Interface\\Glues\\Common\\Glues-BigButton-Down.blp"

-- Screen dimensions in 0-1 space
local VW, VH = 1024, 768
local function n(px) return px / VW end  -- normalise x pixel
local function nv(px) return px / VH end -- normalise y pixel

local LOGIN_SCREEN = {
    account_name = "",
    visible = false,
}

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
        if #LOGIN_SCREEN.account_name < 16 then
            LOGIN_SCREEN.account_name = LOGIN_SCREEN.account_name .. text
        end
    end
end

local function draw_editbox(x, y, w, h)
    -- edge = 16px as per AccountLogin.xml EdgeSize
    local esz = n(16)
    ow3.draw_backdrop(EDITBOX_BG, EDITBOX_EDGE, x, y, w, h, esz)
end

local function draw_button(label, x, y, w, h, pushed)
    ow3.draw_image(pushed and BUTTON_DOWN or BUTTON_UP, x, y, w, h)
    ow3.draw_text(label, x, y, w, h, 14, 255, 215, 0, 255, 'center')
end

function ow3_draw_login_screen()
    if not LOGIN_SCREEN.visible then return end

    ow3.draw_image(BACKGROUND, 0, 0, 1, 1)

    -- WoW logo (top-left, 256×128 as in AccountLogin.xml)
    ow3.draw_image(LOGO, n(3), nv(7), n(256), nv(128))

    -- Account Name edit box
    -- AccountLogin.xml: BOTTOM anchor y=345, size 160×37, centered at x=512+8
    local ex = n(512 + 8 - 80)
    local ey = nv(768 - 345 - 37)
    local ew = n(160)
    local eh = nv(37)

    ow3.draw_text('Account Name:', ex, ey - nv(28), ew, nv(20),
                  14, 255, 215, 0, 255, 'center')
    draw_editbox(ex, ey, ew, eh)
    ow3.draw_text(LOGIN_SCREEN.account_name, ex + n(15), ey + nv(4),
                  ew - n(20), eh - nv(8), 14, 255, 255, 255, 255, 'left')

    -- Blinking cursor
    if math.floor((ow3.time() / 500) % 2) == 0 then
        local cx = ex + n(15) + #LOGIN_SCREEN.account_name * n(9)
        ow3.draw_text('|', cx, ey + nv(4), n(10), eh - nv(8),
                      14, 255, 255, 255, 200, 'left')
    end

    -- Log In button (GlueButtonTemplate: 128×26, TOP anchor y=-519)
    -- TOP of 768px screen minus 519 = y 249, centered at x=512+8
    local bw = n(128)
    local bh = nv(26)
    local bx = n(512 + 8 - 64)
    local by = nv(519)
    draw_button('Log In', bx, by, bw, bh, false)
end
