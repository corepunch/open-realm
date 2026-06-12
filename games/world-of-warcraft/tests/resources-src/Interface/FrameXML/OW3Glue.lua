local last_mouse_x = 0
local last_mouse_y = 0

function ow3_update(msec)
    local p = ow3.player()
    if p and p.client_ui_state == 1 then
        ow3_update_hud(msec)
    end
end

function ow3_draw()
    local p = ow3.player()

    ow3_draw_login_screen()
    ow3_draw_character_select_screen()
    ow3_draw_character_create_screen()

    if p and p.client_ui_state == 1 then
        ow3_draw_hud()
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
