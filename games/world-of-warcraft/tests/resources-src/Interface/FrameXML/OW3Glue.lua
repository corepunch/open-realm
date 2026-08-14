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
    if button ~= 1 then return end
    local messages = ow3.messages()
    local unread = 0
    for _, message in ipairs(messages) do
        if message.flags % 2 == 1 then
            local icon_x = 360 + unread * 42
            if x >= icon_x / 1024 and x <= (icon_x + 32) / 1024 and y >= 680 / 768 and y <= 712 / 768 then
                open_message = message
                ow3.command('message_read ' .. message.id)
                return
            end
            unread = unread + 1
        end
    end
end

function ow3_handle_mouse_move(x, y)
end
