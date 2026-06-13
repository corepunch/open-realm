GlueScreenInfo = { };
GlueScreenInfo["login"] = "AccountLogin";
GlueScreenInfo["charselect"] = "CharacterSelect";

CURRENT_GLUE_SCREEN = nil;

function SetCurrentGlueScreenName(name)
    CURRENT_GLUE_SCREEN = name;
end

function SetGlueScreen(name)
    local newFrame;
    for index, value in GlueScreenInfo do
        local frame = getglobal(value);
        if ( frame ) then
            frame:Hide();
            if ( index == name ) then
                newFrame = frame;
            end
        end
    end
    if ( newFrame ) then
        newFrame:Show();
        SetCurrentScreen(name);
        SetCurrentGlueScreenName(name);
    end
end
