local CHAR_SELECT_SCREEN = {
    visible = false,
    hover_create = false,
    hover_enter = false,
}

function ow3_show_character_select()
    CHAR_SELECT_SCREEN.visible = true
end

function ow3_hide_character_select()
    CHAR_SELECT_SCREEN.visible = false
end

function ow3_character_select_create()
    ow3.command("menu_character_create")
end

function ow3_character_select_enter()
    ow3.load_map("default")
end

function ow3_character_select_handle_click(x, y, button)
    if not CHAR_SELECT_SCREEN.visible then
        return
    end

    local create_x, create_y, create_w, create_h = 0.25, 0.45, 0.2, 0.08
    local enter_x, enter_y, enter_w, enter_h = 0.55, 0.45, 0.2, 0.08

    if button == 1 then
        if x >= create_x and x < create_x + create_w and
           y >= create_y and y < create_y + create_h then
            ow3_character_select_create()
        elseif x >= enter_x and x < enter_x + enter_w and
               y >= enter_y and y < enter_y + enter_h then
            ow3_character_select_enter()
        end
    end
end

function ow3_character_select_handle_mouse_move(x, y)
    if not CHAR_SELECT_SCREEN.visible then
        return
    end

    local create_x, create_y, create_w, create_h = 0.25, 0.45, 0.2, 0.08
    local enter_x, enter_y, enter_w, enter_h = 0.55, 0.45, 0.2, 0.08

    CHAR_SELECT_SCREEN.hover_create =
        x >= create_x and x < create_x + create_w and
        y >= create_y and y < create_y + create_h

    CHAR_SELECT_SCREEN.hover_enter =
        x >= enter_x and x < enter_x + enter_w and
        y >= enter_y and y < enter_y + enter_h
end

function ow3_draw_character_select_screen()
    if not CHAR_SELECT_SCREEN.visible then
        return
    end

    ow3.draw_image('Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp', 0, 0, 1, 1)

    ow3.draw_color(0.3, 0.3, 0.3, 0.4, 0, 0, 0, 200)

    ow3.draw_text('Select Character', 0.2, 0.2, 0.6, 0.1, 32, 255, 215, 120, 255, 'center')

    ow3.draw_text('You have no characters.', 0.25, 0.35, 0.5, 0.04, 16, 220, 220, 220, 255, 'center')
    ow3.draw_text('Create one to begin your adventure!', 0.25, 0.39, 0.5, 0.04, 14, 200, 200, 200, 255, 'center')

    local create_x, create_y, create_w, create_h = 0.25, 0.45, 0.2, 0.08
    local enter_x, enter_y, enter_w, enter_h = 0.55, 0.45, 0.2, 0.08

    local create_bg_color = CHAR_SELECT_SCREEN.hover_create and 80 or 60
    local enter_bg_color = CHAR_SELECT_SCREEN.hover_enter and 80 or 60

    ow3.draw_color(create_x, create_y, create_w, create_h, create_bg_color, 80, 60, 200)
    ow3.draw_text('Create New Character', create_x, create_y, create_w, create_h, 14, 255, 255, 255, 255, 'center')

    ow3.draw_color(enter_x, enter_y, enter_w, enter_h, 60, 80, enter_bg_color, 200)
    ow3.draw_text('Enter World', enter_x, enter_y, enter_w, enter_h, 14, 255, 255, 255, 255, 'center')
end
