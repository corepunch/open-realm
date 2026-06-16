CHARACTER_FACING_INCREMENT = 2;
MAX_RACES = 8;
MAX_CLASSES_PER_RACE = 8;
NUM_CHAR_CUSTOMIZATIONS = 5;
MIN_CHAR_NAME_LENGTH = 2;
CHARACTER_CREATE_ROTATION_START_X = nil;
CHARACTER_CREATE_INITIAL_FACING = nil;
FACTION_BACKDROP_COLOR_TABLE = {
	["Alliance"] = {0.5, 0.5, 0.5, 0.09, 0.09, 0.19},
	["Horde"] = {0.5, 0.2, 0.2, 0.19, 0.05, 0.05},
};
FRAMES_TO_BACKDROP_COLOR = { 
	"CharacterCreateCharacterRace",
	"CharacterCreateCharacterClass",
	"CharacterCreateCharacterFaction",
};
RACE_ICON_TCOORDS = {
	["HUMAN_MALE"]		= {0, 0.25, 0, 0.25},
	["DWARF_MALE"]		= {0.25, 0.5, 0, 0.25},
	["GNOME_MALE"]		= {0.5, 0.75, 0, 0.25},
	["NIGHTELF_MALE"]	= {0.75, 1.0, 0, 0.25},
	
	["TAUREN_MALE"]		= {0, 0.25, 0.25, 0.5},
	["SCOURGE_MALE"]	= {0.25, 0.5, 0.25, 0.5},
	["TROLL_MALE"]		= {0.5, 0.75, 0.25, 0.5},
	["ORC_MALE"]		= {0.75, 1.0, 0.25, 0.5},

	["HUMAN_FEMALE"]	= {0, 0.25, 0.5, 0.75},  
	["DWARF_FEMALE"]	= {0.25, 0.5, 0.5, 0.75},
	["GNOME_FEMALE"]	= {0.5, 0.75, 0.5, 0.75},
	["NIGHTELF_FEMALE"]	= {0.75, 1.0, 0.5, 0.75},
	
	["TAUREN_FEMALE"]	= {0, 0.25, 0.75, 1.0},   
	["SCOURGE_FEMALE"]	= {0.25, 0.5, 0.75, 1.0}, 
	["TROLL_FEMALE"]	= {0.5, 0.75, 0.75, 1.0}, 
	["ORC_FEMALE"]		= {0.75, 1.0, 0.75, 1.0}, 
};
CLASS_ICON_TCOORDS = {
	["WARRIOR"]	= {0, 0.25, 0, 0.25},
	["MAGE"]	= {0.25, 0.49609375, 0, 0.25},
	["ROGUE"]	= {0.49609375, 0.7421875, 0, 0.25},
	["DRUID"]	= {0.7421875, 0.98828125, 0, 0.25},
	["HUNTER"]	= {0, 0.25, 0.25, 0.5},
	["SHAMAN"]	= {0.25, 0.49609375, 0.25, 0.5},
	["PRIEST"]	= {0.49609375, 0.7421875, 0.25, 0.5},
	["WARLOCK"]	= {0.7421875, 0.98828125, 0.25, 0.5},
	["PALADIN"]	= {0, 0.25, 0.5, 0.75}
};

function CharacterCreate_OnLoad()
	this:SetSequence(0);
	this:SetCamera(0);

	CharacterCreate.numRaces = 0;
	CharacterCreate.selectedRace = 0;
	CharacterCreate.numClasses = 0;
	CharacterCreate.selectedClass = 0;
	CharacterCreate.selectedGender = 0;

	SetCharCustomizeFrame("CharacterCreate");
	--CharCreateModel:SetLight(1, 0, 0, -0.707, -0.707, 0.7, 1.0, 1.0, 1.0, 0.8, 1.0, 1.0, 0.8);

	for i=1, NUM_CHAR_CUSTOMIZATIONS, 1 do
		getglobal("CharacterCustomizationButtonFrame"..i.."Text"):SetText(TEXT(getglobal("CHAR_CUSTOMIZATION"..i.."_DESC")));
	end

	-- Color edit box backdrop
	local backdropColor = FACTION_BACKDROP_COLOR_TABLE["Alliance"];
	CharacterCreateNameEdit:SetBackdropBorderColor(backdropColor[1], backdropColor[2], backdropColor[3]);
	CharacterCreateNameEdit:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6]);
end

function CharacterCreate_OnShow()
	--randomly selects a combination
	ResetCharCustomize();

	CharacterCreateEnumerateRaces(GetAvailableRaces());
	SetCharacterRace(GetSelectedRace());

	SetCharacterGender(GetSelectedSex())
	
	SetCharacterClass(1);


	CharacterCreateEnumerateClasses(GetClassesForRace());
	
	CharacterCreateNameEdit:SetText("");
	SetCharacterCreateFacing(-15);
end

function CharacterCreateFrame_OnMouseDown(button)
	if ( button == "LeftButton" ) then
		CHARACTER_CREATE_ROTATION_START_X = GetCursorPosition();
		CHARACTER_CREATE_INITIAL_FACING = GetCharacterCreateFacing();
	end
end

function CharacterCreateFrame_OnMouseUp(button)
	if ( button == "LeftButton" ) then
		CHARACTER_CREATE_ROTATION_START_X = nil
	end
end

function CharacterCreateFrame_OnUpdate()
	if ( CHARACTER_CREATE_ROTATION_START_X ) then
		local x = GetCursorPosition();
		local diff = (x - CHARACTER_CREATE_ROTATION_START_X) * CHARACTER_ROTATION_CONSTANT;
		CHARACTER_CREATE_ROTATION_START_X = GetCursorPosition();
		SetCharacterCreateFacing(GetCharacterCreateFacing() + diff);
	end
end

function CharacterCreateEnumerateRaces(...)
	CharacterCreate.numRaces = arg.n/2;
	if ( arg.n/2 > MAX_RACES ) then
		message("Too many races!  Update MAX_RACES");
		return;
	end
	local coords;
	local index = 1;
	local button;
	local gender;
	if ( GetSelectedSex() == 1 ) then
		gender = "MALE";
	else
		gender = "FEMALE";
	end
	for i=1, arg.n, 2 do
		coords = RACE_ICON_TCOORDS[strupper(arg[i+1].."_"..gender)];
		getglobal("CharacterCreateRaceButton"..index.."NormalTexture"):SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
		button = getglobal("CharacterCreateRaceButton"..index);
		button:Show();
		button.tooltip = arg[i];
		index = index + 1;
	end
	for i=arg.n/2 + 1, MAX_RACES, 1 do
		getglobal("CharacterCreateRaceButton"..i):Hide();
	end
end

function CharacterCreateEnumerateClasses(...)
	CharacterCreate.numClasses = arg.n/2;
	if ( arg.n/2 > MAX_CLASSES_PER_RACE ) then
		message("Too many classes!  Update MAX_CLASSES_PER_RACE");
		return;
	end

	local coords;
	local index = 1;
	local button;
	for i=1, arg.n, 2 do
		coords = CLASS_ICON_TCOORDS[strupper(arg[i+1])];
		getglobal("CharacterCreateClassButton"..index.."NormalTexture"):SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
		button = getglobal("CharacterCreateClassButton"..index);
		button:Show();
		button.tooltip = arg[i];
		index = index + 1;
	end
	for i=arg.n/2 + 1, MAX_CLASSES_PER_RACE, 1 do
		getglobal("CharacterCreateClassButton"..i):Hide();
	end
end

function SetCharacterRace(id)
	CharacterCreate.selectedRace = id;
	for i=1, CharacterCreate.numRaces, 1 do
		local button = getglobal("CharacterCreateRaceButton"..i);
		if ( i == id ) then
			getglobal("CharacterCreateRaceButton"..i.."HighlightText"):SetText(button.tooltip);
			button:SetChecked(1);
			button:LockHighlight();
		else
			getglobal("CharacterCreateRaceButton"..i.."HighlightText"):SetText("");
			button:SetChecked(0);
			button:UnlockHighlight();
		end
	end

	--twain SetSelectedRace(id);
	-- Set Faction
	local name, faction = GetFactionForRace();
	if ( faction == "Alliance" ) then
		CharacterCreateFactionIcon:SetTexCoord(0, 0.5, 0, 1.0);
	else
		CharacterCreateFactionIcon:SetTexCoord(0.5, 1.0, 0, 1.0);
	end
	CharacterCreateFactionScrollFrameScrollBar:SetValue(0);
	CharacterCreateFactionLabel:SetText(name);
	CharacterCreateFactionText:SetText(getglobal("FACTION_INFO_"..strupper(faction)));
	CharacterCreateFactionText:SetHeight(CharacterCreateFactionText:GetStringHeight());
	CharacterCreateCharacterFaction:SetHeight(CharacterCreateFactionLabel:GetStringHeight() + CharacterCreateFactionText:GetHeight() + 40);
	CharacterCreateFactionScrollFrame:UpdateScrollChildRect();
	--CharacterCreateCharacterFaction:SetHeight(CharacterCreateFactionText:GetHeight() + 40);

	-- Set Race
	local race, fileString = GetNameForRace();
	CharacterCreateRaceLabel:SetText(race);
	fileString = strupper(fileString);
	if ( GetSelectedSex() == 1 ) then
		gender = "MALE";
	else
		gender = "FEMALE";
	end
	local coords = RACE_ICON_TCOORDS[fileString.."_"..gender];
	CharacterCreateRaceIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
	local raceText = getglobal("RACE_INFO_"..fileString);
	local abilityIndex = 1;
	local tempText = getglobal("ABILITY_INFO_"..fileString..abilityIndex);
	abilityText = "";
	while ( tempText ) do
		abilityText = abilityText..tempText..
		"\n\n";
		abilityIndex = abilityIndex + 1;
		tempText = getglobal("ABILITY_INFO_"..fileString..abilityIndex);
	end


	CharacterCreateRaceScrollFrameScrollBar:SetValue(0);
	if ( abilityText and abilityText ~= "" ) then
		CharacterCreateRaceText:SetText(getglobal("RACE_INFO_"..fileString));
		CharacterCreateRaceAbilityText:SetText(abilityText);
	else
		CharacterCreateRaceText:SetText(getglobal("RACE_INFO_"..fileString));
		CharacterCreateRaceAbilityText:SetText("");
	end
	CharacterCreateRaceText:SetHeight(CharacterCreateRaceText:GetStringHeight());
	CharacterCreateRaceAbilityText:SetHeight(CharacterCreateRaceAbilityText:GetStringHeight());
	CharacterCreateCharacterRace:SetHeight(CharacterCreateRaceLabel:GetStringHeight() + CharacterCreateRaceText:GetHeight() + CharacterCreateRaceAbilityText:GetHeight() + 40);
	CharacterCreateRaceScrollFrame:UpdateScrollChildRect();
	--CharacterCreateCharacterRace:SetHeight(CharacterCreateRaceText:GetHeight() + 40);

	-- Set backdrop colors based on faction
	local backdropColor = FACTION_BACKDROP_COLOR_TABLE[faction];
	local frame;
	for index, value in FRAMES_TO_BACKDROP_COLOR do
		frame = getglobal(value);
		--frame:SetBackdropBorderColor(backdropColor[1], backdropColor[2], backdropColor[3]);
		frame:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6]);
	end

	SetBackgroundModel(CharacterCreate, fileString);
	--twainUpdateCustomizationBackground();
	
	CharacterCreateEnumerateClasses(GetClassesForRace());
	SetCharacterClass(1);

	-- Hair customization stuff
	CharacterCreate_UpdateFacialHairCustomization();
	CharacterCreate_UpdateHairCustomization();
end

function SetCharacterClass(id)
	CharacterCreate.selectedClass = id;
	for i=1, CharacterCreate.numClasses, 1 do
		local button = getglobal("CharacterCreateClassButton"..i);
		if ( i == id ) then
			getglobal("CharacterCreateClassButton"..i.."HighlightText"):SetText(button.tooltip);
			button:SetChecked(1);
			button:LockHighlight();
		else
			getglobal("CharacterCreateClassButton"..i.."HighlightText"):SetText("");
			button:UnlockHighlight();
			button:SetChecked(0);
		end
	end
	
	--twain SetSelectedClass(id);
	local className, classFileName = GetSelectedClass();
	local coords = CLASS_ICON_TCOORDS[classFileName];
	CharacterCreateClassIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
	CharacterCreateClassLabel:SetText(className);
	CharacterCreateClassScrollFrameScrollBar:SetValue(0);
	CharacterCreateClassText:SetText(getglobal("CLASS_"..strupper(classFileName)));
	CharacterCreateClassText:SetHeight(CharacterCreateClassText:GetStringHeight());
	CharacterCreateCharacterClass:SetHeight(CharacterCreateClassLabel:GetStringHeight() + CharacterCreateClassText:GetHeight() + 40);
	CharacterCreateClassScrollFrame:UpdateScrollChildRect();
	--CharacterCreateCharacterClass:SetHeight(CharacterCreateClassText:GetHeight() + 45);
end

function CharacterCreate_OnChar()
end

function CharacterCreate_OnKeyDown()
	if ( arg1 == "ESCAPE" ) then
		CharacterCreate_Back();
	elseif ( arg1 == "ENTER" ) then
		CharacterCreate_Okay();
	elseif ( arg1 == "PRINTSCREEN" ) then
		Screenshot();
	end
end

function CharacterCreate_UpdateModel()
	UpdateCustomizationScene();
	this:AdvanceTime();
end

function CharacterCreate_Okay()
	PlaySound("gsCharacterCreationCreateChar");
	CreateCharacter(CharacterCreateNameEdit:GetText());
end

function CharacterCreate_Back()
	PlaySound("gsCharacterCreationCancel");
	SetGlueScreen("charselect");
end

function CharacterClass_OnClick(id)
	PlaySound("gsCharacterCreationClass");
	SetSelectedClass(id);
	SetCharacterClass(id);
end

function CharacterRace_OnClick(id)
	PlaySound("gsCharacterCreationClass");
	if ( not this:GetChecked() ) then
		this:SetChecked(1);
		return;
	end
	SetSelectedRace(id);
	SetCharacterRace(id);
	SetSelectedSex(GetSelectedSex());
	SetCharacterCreateFacing(-15);
end

function SetCharacterGender(sex)
	local gender;
	SetSelectedSex(sex);
	if ( sex == 1 ) then
		gender = "MALE";
		CharacterCreateGenderButtonMaleHighlightText:SetText(MALE);
		CharacterCreateGenderButtonMale:SetChecked(1);
		CharacterCreateGenderButtonMale:LockHighlight();
		CharacterCreateGenderButtonFemaleHighlightText:SetText("");
		CharacterCreateGenderButtonFemale:SetChecked(nil);
		CharacterCreateGenderButtonFemale:UnlockHighlight();
	else
		gender = "FEMALE";
		CharacterCreateGenderButtonMaleHighlightText:SetText("");
		CharacterCreateGenderButtonMale:SetChecked(nil);
		CharacterCreateGenderButtonMale:UnlockHighlight();
		CharacterCreateGenderButtonFemaleHighlightText:SetText(FEMALE);
		CharacterCreateGenderButtonFemale:SetChecked(1);
		CharacterCreateGenderButtonFemale:LockHighlight();
	end
	
	--twain SetSelectedSex(id);
	-- Update race images to reflect gender
	CharacterCreateEnumerateRaces(GetAvailableRaces());

	-- Update facial hair customization since gender can affect this
	CharacterCreate_UpdateFacialHairCustomization();

	-- Update right hand race portrait to reflect gender change
	-- Set Race
	local race, fileString = GetNameForRace();
	CharacterCreateRaceLabel:SetText(race);
	fileString = strupper(fileString);
	local coords = RACE_ICON_TCOORDS[fileString.."_"..gender];
	CharacterCreateRaceIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
end

function CharacterCustomization_Left(id)
	PlaySound("gsCharacterCreationLook");
	CycleCharCustomization(id, -1);
end

function CharacterCustomization_Right(id)
	PlaySound("gsCharacterCreationLook");
	CycleCharCustomization(id, 1);
end

function CharacterCreate_Randomize()
	PlaySound("gsCharacterCreationLook");
	RandomizeCharCustomization();
end

function CharacterCreateRotateRight_OnUpdate()
	if ( this:GetButtonState() == "PUSHED" ) then
		SetCharacterCreateFacing(GetCharacterCreateFacing() + CHARACTER_FACING_INCREMENT);
	end
end

function CharacterCreateRotateLeft_OnUpdate()
	if ( this:GetButtonState() == "PUSHED" ) then
		SetCharacterCreateFacing(GetCharacterCreateFacing() - CHARACTER_FACING_INCREMENT);
	end
end

function CharacterCreate_UpdateFacialHairCustomization()
	if ( GetFacialHairCustomization() == "NONE" ) then
		CharacterCustomizationButtonFrame5:Hide();
		CharCreateRandomizeButton:SetPoint("TOP", "CharacterCustomizationButtonFrame5", "BOTTOM", 0, -5);
	else
		CharacterCustomizationButtonFrame5Text:SetText(getglobal("FACIAL_HAIR_"..GetFacialHairCustomization()));		
		CharacterCustomizationButtonFrame5:Show();
		CharCreateRandomizeButton:SetPoint("TOP", "CharacterCustomizationButtonFrame5", "BOTTOM", 0, -5);
	end
end

function CharacterCreate_UpdateHairCustomization()
	CharacterCustomizationButtonFrame3Text:SetText(getglobal("HAIR_"..GetHairCustomization().."_STYLE"));
	CharacterCustomizationButtonFrame4Text:SetText(getglobal("HAIR_"..GetHairCustomization().."_COLOR"));
end
