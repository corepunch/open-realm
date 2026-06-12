local elapsed = 0
local last_mouse_x = 0
local last_mouse_y = 0

function ow3_update(msec)
    elapsed = elapsed + msec
end

function ow3_draw()
    local p = ow3.player()

    ow3_draw_login_screen()
    ow3_draw_character_select_screen()
    ow3_draw_character_create_screen()

    if p and p.client_ui_state == 1 then
        local inv = ow3.inventory()[1]
        ow3.draw_image('Interface\\Test\\LuaPanel.blp', 0.100, 0.200, 0.300, 0.050)
        if inv and inv.image > 0 then
            ow3.draw_image_index(inv.image, 0.140, 0.260, 0.040, 0.040)
        end
        ow3.draw_color(0.200, 0.320, 0.120, 0.020, 10, 20, 30, 240)
        ow3.draw_text(p.name .. ':' .. p.health .. ':' .. elapsed, 0.210, 0.350, 0.400, 0.030, 13, 255, 220, 120, 255, 'center')
        ow3.draw_minimap(0.700, 0.080, 0.120, 0.120)
    end
end

function ow3_handle_text_input(text)
    ow3_login_handle_text(text)
    ow3_character_create_handle_text(text)
end

function ow3_handle_mouse_click(x, y, button)
    ow3_character_select_handle_click(x, y, button)
    ow3_character_create_handle_click(x, y, button)
end

function ow3_handle_mouse_move(x, y)
    last_mouse_x = x
    last_mouse_y = y
    ow3_character_select_handle_mouse_move(x, y)
    ow3_character_create_handle_mouse_move(x, y)
end
