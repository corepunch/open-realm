local BAR_BG     = "Interface\\Glues\\LoadingBar\\Loading-BarBackground.blp"
local BAR_FILL   = "Interface\\Glues\\LoadingBar\\Loading-BarFill.blp"
local BAR_GLASS  = "Interface\\Glues\\LoadingBar\\Loading-BarGlass.blp"
local BAR_BORDER = "Interface\\Glues\\LoadingBar\\Loading-BarBorder.blp"
local BAR_GLOW   = "Interface\\Glues\\LoadingBar\\Loading-BarGlow.blp"

local BAR_X, BAR_Y, BAR_W, BAR_H           = 0.305, 0.865, 0.39,  0.032
local BORDER_X, BORDER_Y, BORDER_W, BORDER_H = 0.285, 0.848, 0.43,  0.067
local TITLE_X, TITLE_Y, TITLE_W, TITLE_H   = 0.16,  0.77,  0.68,  0.05
local STATUS_X, STATUS_Y, STATUS_W, STATUS_H = 0.16,  0.818, 0.68,  0.04

function ow3_draw_loading_screen()
    local progress = ow3.get_loading_progress()
    local title    = ow3.get_loading_title()
    local status   = ow3.get_loading_status()

    ow3.draw_loading_background()
    ow3.draw_image(BAR_BG, BAR_X, BAR_Y, BAR_W, BAR_H)

    if progress > 0 then
        ow3.draw_image_uv(BAR_FILL, BAR_X, BAR_Y, BAR_W * progress, BAR_H,
                          0, progress, 0, 1)
    end

    ow3.draw_image_additive(BAR_GLOW,   BORDER_X, BORDER_Y, BORDER_W, BORDER_H,
                            255, 255, 255, 200)
    ow3.draw_image(BAR_GLASS,  BORDER_X, BORDER_Y, BORDER_W, BORDER_H)
    ow3.draw_image(BAR_BORDER, BORDER_X, BORDER_Y, BORDER_W, BORDER_H)

    if title and #title > 0 then
        ow3.draw_text(title, TITLE_X, TITLE_Y, TITLE_W, TITLE_H,
                      22, 235, 210, 160, 255, 'center')
    end
    if status and #status > 0 then
        ow3.draw_text(status, STATUS_X, STATUS_Y, STATUS_W, STATUS_H,
                      16, 220, 220, 220, 255, 'center')
    end
end
