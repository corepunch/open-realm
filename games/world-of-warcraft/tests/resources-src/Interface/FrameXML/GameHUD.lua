local W = ow3
local VW, VH = 1024, 768
local elapsed = 0

local function px(x, y, w, h) return x / VW, y / VH, w / VW, h / VH end
local function image(path, x, y, w, h) W.draw_image(path, px(x, y, w, h)) end
local function image_uv(path, x, y, w, h, l, r, t, b)
    W.draw_image_uv(path, x / VW, y / VH, w / VW, h / VH, l, r, t, b)
end
local function icon(index, x, y, w, h)
    if index and index > 0 then W.draw_image_index(index, px(x, y, w, h)) end
end
local function bar(x, y, w, h, value, maxvalue, r, g, b)
    W.draw_color(x / VW, y / VH, w / VW, h / VH, 12, 10, 8, 220)
    local p = maxvalue > 0 and value / maxvalue or 0
    if p < 0 then p = 0 elseif p > 1 then p = 1 end
    W.draw_color((x + 2) / VW, (y + 2) / VH, ((w - 4) * p) / VW, (h - 4) / VH, r, g, b, 235)
end
local function text(str, x, y, w, h, size, r, g, b, a, align)
    W.draw_text(str or '', x / VW, y / VH, w / VW, h / VH, size, r, g, b, a or 255, align or 'left')
end
local function action_button(action, x, y)
    image('Interface\\Buttons\\UI-Quickslot2.blp', x - 14, y - 13, 64, 64)
    icon(action and action.image, x + 2, y + 2, 32, 32)
    if action and action.count and action.count > 1 then
        text(tostring(action.count), x + 2, y + 23, 32, 10, 10, 255, 255, 255, 255, 'right')
    end
end

function ow3_update_hud(msec)
    elapsed = elapsed + msec
    local p = W.player()
    W.command('wow_lua_test ' .. p.level .. ' ' .. elapsed)
end

function ow3_draw_hud()
    local p = W.player()
    local inv = W.inventory()

    W.draw_image('Interface\\Test\\LuaPanel.blp', 0.100, 0.200, 0.300, 0.050)
    local first_inv = inv[1]
    if first_inv and first_inv.image > 0 then
        W.draw_image_index(first_inv.image, 0.140, 0.260, 0.040, 0.040)
    end
    W.draw_color(0.200, 0.320, 0.120, 0.020, 10, 20, 30, 240)
    text(p.name .. ':' .. p.health .. ':' .. elapsed, 0.210, 0.350, 0.400, 0.030,
         13, 255, 220, 120, 255, 'center')
    W.draw_minimap(896 / VW, 25 / VH, 91 / VW, 91 / VH)
end
