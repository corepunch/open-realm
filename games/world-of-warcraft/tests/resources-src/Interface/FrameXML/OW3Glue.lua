function ow3_update(msec)
    local p = ow3.player()
    if p and p.client_ui_state == 1 then
        ow3_update_hud(msec)
    end
end

function ow3_draw()
    local p = ow3.player()

    if p and p.client_ui_state == 1 then
        ow3_draw_hud()
    end
end

function ow3_handle_text_input(text)
end

function ow3_handle_mouse_click(x, y, button)
end

function ow3_handle_mouse_move(x, y)
end
