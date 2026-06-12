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
        if #LOGIN_SCREEN.account_name < 32 then
            LOGIN_SCREEN.account_name = LOGIN_SCREEN.account_name .. text
        end
    end
end

function ow3_draw_login_screen()
    if not LOGIN_SCREEN.visible then
        return
    end

    local VW, VH = 1024, 768

    ow3.draw_color(0, 0, 1, 1, 0, 0, 0, 200)

    ow3.draw_text('World of Warcraft', 0.2, 0.2, 0.6, 0.1, 32, 255, 215, 120, 255, 'center')

    ow3.draw_text('Account Name:', 0.25, 0.35, 0.5, 0.04, 16, 220, 220, 220, 255, 'left')

    ow3.draw_color(0.25, 0.40, 0.5, 0.05, 50, 50, 50, 200)
    ow3.draw_text(LOGIN_SCREEN.account_name, 0.27, 0.41, 0.46, 0.04, 14, 255, 255, 255, 255, 'left')

    if math.floor((ow3.time() / 500) % 2) == 0 then
        ow3.draw_text('_', 0.27 + (#LOGIN_SCREEN.account_name * 0.008), 0.41, 0.02, 0.04, 14, 255, 255, 255, 200, 'left')
    end

    ow3.draw_color(0.35, 0.50, 0.3, 0.05, 60, 100, 60, 200)
    ow3.draw_text('LOGIN', 0.35, 0.50, 0.3, 0.05, 16, 255, 255, 255, 255, 'center')
end
