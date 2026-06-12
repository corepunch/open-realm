local CHAR_CREATE_SCREEN = {
    visible = false,
    character = {
        name = "",
        race = 1,
        class = 1,
        skin_color = 1,
        hair_style = 1,
        hair_color = 1,
        facial_hair = 0,
    },
    races = { "Human", "Orc", "Dwarf", "Night Elf", "Tauren", "Gnome", "Troll", "Undead" },
    classes = { "Warrior", "Paladin", "Hunter", "Rogue", "Priest", "Shaman", "Druid", "Warlock", "Monk" },
    skin_colors = { "Light", "Medium", "Dark", "Very Dark" },
    hair_styles = { "Style 1", "Style 2", "Style 3", "Style 4", "Style 5" },
    hair_colors = { "Black", "Brown", "Blonde", "Red", "White", "Blue" },
    facial_hairs = { "None", "Beard 1", "Beard 2", "Beard 3" },

    hover_create_btn = false,
    hover_cancel_btn = false,
    scroll_pos = 0,
}

function ow3_show_character_create()
    CHAR_CREATE_SCREEN.visible = true
    CHAR_CREATE_SCREEN.character = {
        name = "",
        race = 1,
        class = 1,
        skin_color = 1,
        hair_style = 1,
        hair_color = 1,
        facial_hair = 0,
    }
end

function ow3_hide_character_create()
    CHAR_CREATE_SCREEN.visible = false
end

function ow3_character_create_prev_race()
    CHAR_CREATE_SCREEN.character.race = CHAR_CREATE_SCREEN.character.race - 1
    if CHAR_CREATE_SCREEN.character.race < 1 then
        CHAR_CREATE_SCREEN.character.race = #CHAR_CREATE_SCREEN.races
    end
end

function ow3_character_create_next_race()
    CHAR_CREATE_SCREEN.character.race = CHAR_CREATE_SCREEN.character.race + 1
    if CHAR_CREATE_SCREEN.character.race > #CHAR_CREATE_SCREEN.races then
        CHAR_CREATE_SCREEN.character.race = 1
    end
end

function ow3_character_create_prev_class()
    CHAR_CREATE_SCREEN.character.class = CHAR_CREATE_SCREEN.character.class - 1
    if CHAR_CREATE_SCREEN.character.class < 1 then
        CHAR_CREATE_SCREEN.character.class = #CHAR_CREATE_SCREEN.classes
    end
end

function ow3_character_create_next_class()
    CHAR_CREATE_SCREEN.character.class = CHAR_CREATE_SCREEN.character.class + 1
    if CHAR_CREATE_SCREEN.character.class > #CHAR_CREATE_SCREEN.classes then
        CHAR_CREATE_SCREEN.character.class = 1
    end
end

function ow3_character_create_prev_skin()
    CHAR_CREATE_SCREEN.character.skin_color = CHAR_CREATE_SCREEN.character.skin_color - 1
    if CHAR_CREATE_SCREEN.character.skin_color < 1 then
        CHAR_CREATE_SCREEN.character.skin_color = #CHAR_CREATE_SCREEN.skin_colors
    end
end

function ow3_character_create_next_skin()
    CHAR_CREATE_SCREEN.character.skin_color = CHAR_CREATE_SCREEN.character.skin_color + 1
    if CHAR_CREATE_SCREEN.character.skin_color > #CHAR_CREATE_SCREEN.skin_colors then
        CHAR_CREATE_SCREEN.character.skin_color = 1
    end
end

function ow3_character_create_prev_hair_style()
    CHAR_CREATE_SCREEN.character.hair_style = CHAR_CREATE_SCREEN.character.hair_style - 1
    if CHAR_CREATE_SCREEN.character.hair_style < 1 then
        CHAR_CREATE_SCREEN.character.hair_style = #CHAR_CREATE_SCREEN.hair_styles
    end
end

function ow3_character_create_next_hair_style()
    CHAR_CREATE_SCREEN.character.hair_style = CHAR_CREATE_SCREEN.character.hair_style + 1
    if CHAR_CREATE_SCREEN.character.hair_style > #CHAR_CREATE_SCREEN.hair_styles then
        CHAR_CREATE_SCREEN.character.hair_style = 1
    end
end

function ow3_character_create_prev_hair_color()
    CHAR_CREATE_SCREEN.character.hair_color = CHAR_CREATE_SCREEN.character.hair_color - 1
    if CHAR_CREATE_SCREEN.character.hair_color < 1 then
        CHAR_CREATE_SCREEN.character.hair_color = #CHAR_CREATE_SCREEN.hair_colors
    end
end

function ow3_character_create_next_hair_color()
    CHAR_CREATE_SCREEN.character.hair_color = CHAR_CREATE_SCREEN.character.hair_color + 1
    if CHAR_CREATE_SCREEN.character.hair_color > #CHAR_CREATE_SCREEN.hair_colors then
        CHAR_CREATE_SCREEN.character.hair_color = 1
    end
end

function ow3_character_create_prev_facial_hair()
    CHAR_CREATE_SCREEN.character.facial_hair = CHAR_CREATE_SCREEN.character.facial_hair - 1
    if CHAR_CREATE_SCREEN.character.facial_hair < 0 then
        CHAR_CREATE_SCREEN.character.facial_hair = #CHAR_CREATE_SCREEN.facial_hairs - 1
    end
end

function ow3_character_create_next_facial_hair()
    CHAR_CREATE_SCREEN.character.facial_hair = CHAR_CREATE_SCREEN.character.facial_hair + 1
    if CHAR_CREATE_SCREEN.character.facial_hair > #CHAR_CREATE_SCREEN.facial_hairs - 1 then
        CHAR_CREATE_SCREEN.character.facial_hair = 0
    end
end

function ow3_character_create_handle_text(text)
    if text == "\r" or text == "\n" then
        if #CHAR_CREATE_SCREEN.character.name > 0 then
            ow3_character_create_create()
        end
    elseif text == "\b" then
        if #CHAR_CREATE_SCREEN.character.name > 0 then
            CHAR_CREATE_SCREEN.character.name = CHAR_CREATE_SCREEN.character.name:sub(1, -2)
        end
    elseif #text == 1 and text >= "a" and text <= "z" then
        if #CHAR_CREATE_SCREEN.character.name < 16 then
            CHAR_CREATE_SCREEN.character.name = CHAR_CREATE_SCREEN.character.name .. text:upper()
        end
    elseif #text == 1 and text >= "A" and text <= "Z" then
        if #CHAR_CREATE_SCREEN.character.name < 16 then
            CHAR_CREATE_SCREEN.character.name = CHAR_CREATE_SCREEN.character.name .. text
        end
    end
end

function ow3_character_create_handle_click(x, y, button)
    if not CHAR_CREATE_SCREEN.visible or button ~= 1 then
        return
    end

    local create_btn_x, create_btn_y, create_btn_w, create_btn_h = 0.3, 0.8, 0.15, 0.05
    local cancel_btn_x, cancel_btn_y, cancel_btn_w, cancel_btn_h = 0.55, 0.8, 0.15, 0.05

    if x >= create_btn_x and x < create_btn_x + create_btn_w and
       y >= create_btn_y and y < create_btn_y + create_btn_h then
        if #CHAR_CREATE_SCREEN.character.name > 0 then
            ow3_character_create_create()
        end
    elseif x >= cancel_btn_x and x < cancel_btn_x + cancel_btn_w and
           y >= cancel_btn_y and y < cancel_btn_y + cancel_btn_h then
        ow3.command("menu_character_select")
    end
end

function ow3_character_create_handle_mouse_move(x, y)
    if not CHAR_CREATE_SCREEN.visible then
        return
    end

    local create_btn_x, create_btn_y, create_btn_w, create_btn_h = 0.3, 0.8, 0.15, 0.05
    local cancel_btn_x, cancel_btn_y, cancel_btn_w, cancel_btn_h = 0.55, 0.8, 0.15, 0.05

    CHAR_CREATE_SCREEN.hover_create_btn =
        x >= create_btn_x and x < create_btn_x + create_btn_w and
        y >= create_btn_y and y < create_btn_y + create_btn_h

    CHAR_CREATE_SCREEN.hover_cancel_btn =
        x >= cancel_btn_x and x < cancel_btn_x + cancel_btn_w and
        y >= cancel_btn_y and y < cancel_btn_y + cancel_btn_h
end

function ow3_character_create_create()
    ow3.load_map("default")
end

function ow3_draw_selector_button(x, y, w, h, text, hover)
    local bg_color = hover and 100 or 70
    ow3.draw_color(x, y, w, h, bg_color, bg_color, bg_color, 200)
    ow3.draw_text(text, x, y, w, h, 12, 255, 255, 255, 255, 'center')
end

function ow3_draw_character_create_screen()
    if not CHAR_CREATE_SCREEN.visible then
        return
    end

    ow3.draw_image('Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp', 0, 0, 1, 1)

    ow3.draw_color(0.3, 0.3, 0.3, 0.4, 0, 0, 0, 200)

    ow3.draw_text('Create New Character', 0.2, 0.05, 0.6, 0.08, 28, 255, 215, 120, 255, 'center')

    local col1_x = 0.05
    local col2_x = 0.55
    local row_h = 0.08

    local y = 0.15

    ow3.draw_text('Race:', col1_x, y, 0.3, row_h * 0.6, 14, 220, 220, 220, 255, 'left')
    ow3.draw_text(CHAR_CREATE_SCREEN.races[CHAR_CREATE_SCREEN.character.race], col1_x + 0.15, y, 0.3, row_h * 0.6, 14, 255, 255, 255, 255, 'left')
    ow3_draw_selector_button(col1_x + 0.35, y, 0.08, 0.04, '<', false)
    ow3_draw_selector_button(col1_x + 0.44, y, 0.08, 0.04, '>', false)

    y = y + row_h

    ow3.draw_text('Class:', col1_x, y, 0.3, row_h * 0.6, 14, 220, 220, 220, 255, 'left')
    ow3.draw_text(CHAR_CREATE_SCREEN.classes[CHAR_CREATE_SCREEN.character.class], col1_x + 0.15, y, 0.3, row_h * 0.6, 14, 255, 255, 255, 255, 'left')
    ow3_draw_selector_button(col1_x + 0.35, y, 0.08, 0.04, '<', false)
    ow3_draw_selector_button(col1_x + 0.44, y, 0.08, 0.04, '>', false)

    y = y + row_h

    ow3.draw_text('Skin:', col1_x, y, 0.3, row_h * 0.6, 14, 220, 220, 220, 255, 'left')
    ow3.draw_text(CHAR_CREATE_SCREEN.skin_colors[CHAR_CREATE_SCREEN.character.skin_color], col1_x + 0.15, y, 0.3, row_h * 0.6, 14, 255, 255, 255, 255, 'left')
    ow3_draw_selector_button(col1_x + 0.35, y, 0.08, 0.04, '<', false)
    ow3_draw_selector_button(col1_x + 0.44, y, 0.08, 0.04, '>', false)

    y = y + row_h

    ow3.draw_text('Hair:', col1_x, y, 0.3, row_h * 0.6, 14, 220, 220, 220, 255, 'left')
    ow3.draw_text(CHAR_CREATE_SCREEN.hair_styles[CHAR_CREATE_SCREEN.character.hair_style], col1_x + 0.15, y, 0.3, row_h * 0.6, 14, 255, 255, 255, 255, 'left')
    ow3_draw_selector_button(col1_x + 0.35, y, 0.08, 0.04, '<', false)
    ow3_draw_selector_button(col1_x + 0.44, y, 0.08, 0.04, '>', false)

    y = y + row_h

    ow3.draw_text('Hair Color:', col1_x, y, 0.3, row_h * 0.6, 14, 220, 220, 220, 255, 'left')
    ow3.draw_text(CHAR_CREATE_SCREEN.hair_colors[CHAR_CREATE_SCREEN.character.hair_color], col1_x + 0.15, y, 0.3, row_h * 0.6, 14, 255, 255, 255, 255, 'left')
    ow3_draw_selector_button(col1_x + 0.35, y, 0.08, 0.04, '<', false)
    ow3_draw_selector_button(col1_x + 0.44, y, 0.08, 0.04, '>', false)

    y = y + row_h

    ow3.draw_text('Facial Hair:', col1_x, y, 0.3, row_h * 0.6, 14, 220, 220, 220, 255, 'left')
    ow3.draw_text(CHAR_CREATE_SCREEN.facial_hairs[CHAR_CREATE_SCREEN.character.facial_hair + 1], col1_x + 0.15, y, 0.3, row_h * 0.6, 14, 255, 255, 255, 255, 'left')
    ow3_draw_selector_button(col1_x + 0.35, y, 0.08, 0.04, '<', false)
    ow3_draw_selector_button(col1_x + 0.44, y, 0.08, 0.04, '>', false)

    y = y + row_h

    ow3.draw_text('Name:', col1_x, y, 0.3, row_h * 0.6, 14, 220, 220, 220, 255, 'left')
    ow3.draw_color(col1_x + 0.15, y, 0.3, 0.04, 50, 50, 50, 200)
    ow3.draw_text(CHAR_CREATE_SCREEN.character.name, col1_x + 0.17, y, 0.26, 0.04, 14, 255, 255, 255, 255, 'left')

    if math.floor((ow3.time() / 500) % 2) == 0 then
        ow3.draw_text('_', col1_x + 0.17 + (#CHAR_CREATE_SCREEN.character.name * 0.015), y, 0.02, 0.04, 14, 255, 255, 255, 200, 'left')
    end

    local create_btn_color = CHAR_CREATE_SCREEN.hover_create_btn and 100 or 70
    local cancel_btn_color = CHAR_CREATE_SCREEN.hover_cancel_btn and 100 or 70

    ow3.draw_color(0.3, 0.8, 0.15, 0.05, create_btn_color, create_btn_color, create_btn_color, 200)
    ow3.draw_text('CREATE', 0.3, 0.8, 0.15, 0.05, 14, 255, 255, 255, 255, 'center')

    ow3.draw_color(0.55, 0.8, 0.15, 0.05, cancel_btn_color, cancel_btn_color, cancel_btn_color, 200)
    ow3.draw_text('CANCEL', 0.55, 0.8, 0.15, 0.05, 14, 255, 255, 255, 255, 'center')
end
