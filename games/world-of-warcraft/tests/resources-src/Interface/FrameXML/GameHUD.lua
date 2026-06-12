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

    -- Player frame
    W.draw_image_uv('Interface\\TargetingFrame\\UI-TargetingFrame.blp',
                    -19 / VW, 4 / VH, 232 / VW, 100 / VH,
                    1.0, 0.09375, 0, 0.78125, 96, 92, 84, 230)
    W.draw_color(87 / VW, 22 / VH, 119 / VW, 41 / VH, 0, 0, 0, 128)
    text(p.name,            72,  18, 100, 12, 13, 255, 215, 120, 255, 'center')
    text('Lvl ' .. p.level, 24,  58,  42, 12, 11, 235, 225, 190, 255, 'center')
    bar(105, 41, 119, 12, p.health, p.healthMax, 20, 178,  48)
    bar(105, 54, 119, 11, p.power,  p.powerMax,  26,  82, 210)

    -- Unit panel (shows currently selected unit's info texture)
    W.draw_image('Interface\\Test\\LuaPanel.blp', 0.100, 0.200, 0.300, 0.050)
    local first_inv = inv[1]
    if first_inv and first_inv.image > 0 then
        W.draw_image_index(first_inv.image, 0.140, 0.260, 0.040, 0.040)
    end
    W.draw_color(0.200, 0.320, 0.120, 0.020, 10, 20, 30, 240)
    text(p.name .. ':' .. p.health .. ':' .. elapsed, 0.210, 0.350, 0.400, 0.030,
         13, 255, 220, 120, 255, 'center')

    -- Main menu bar
    image_uv('Interface\\MainMenuBar\\UI-MainMenuBar-Dwarf.blp',   0, 715, 256, 53, 0, 1.0, 0.79296875, 1.0)
    image_uv('Interface\\MainMenuBar\\UI-MainMenuBar-Dwarf.blp', 256, 715, 256, 53, 0, 1.0, 0.54296875, 0.75)
    image_uv('Interface\\MainMenuBar\\UI-MainMenuBar-Dwarf.blp', 512, 715, 256, 53, 0, 1.0, 0.29296875, 0.5)
    image_uv('Interface\\MainMenuBar\\UI-MainMenuBar-Dwarf.blp', 768, 715, 256, 53, 0, 1.0, 0.04296875, 0.25)
    image('Interface\\MainMenuBar\\UI-MainMenuBar-EndCap-Dwarf.blp',  -96, 640, 128, 128)
    image_uv('Interface\\MainMenuBar\\UI-MainMenuBar-EndCap-Dwarf.blp', 992, 640, 128, 128, 1.0, 0.0, 0.0, 1.0)

    -- Action buttons (image indexes registered by server via ImageIndex)
    local actions = W.actions()
    for i = 0, 11 do
        action_button(actions[i + 1], 8 + i * 42, 728)
    end
    for i = 0, 3 do
        action_button(nil, 939 - i * 42, 728)
    end

    -- Bag, minimap, quest log
    image('Interface\\Buttons\\Button-Backpack-Up.blp',      981, 729, 37,  37)
    image('Interface\\Minimap\\UI-Minimap-Border.blp',       879,   8, 128, 128)
    W.draw_minimap(896 / VW, 25 / VH, 91 / VW, 91 / VH)
    image('Interface\\QuestFrame\\UI-QuestLog-BookIcon.blp',  840, 162, 32,  32)
    text('Quests',               876, 164, 110, 20, 13, 255, 215, 120, 255)
    text('Copper ' .. p.copper,  816, 704, 150, 20, 12, 255, 210, 100, 255, 'right')
end
