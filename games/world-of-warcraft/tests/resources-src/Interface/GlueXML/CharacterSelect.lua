CHARACTER_SELECT_ROTATION_START_X = nil;
CHARACTER_SELECT_INITIAL_FACING = nil;

CHARACTER_ROTATION_CONSTANT = 0.6;

MAX_CHARACTERS_DISPLAYED = 10;
MAX_CHARACTERS_PER_REALM = 10;


function CharacterSelect_OnLoad()
	this:SetSequence(0);
	this:SetCamera(0);

	this.createIndex = 0;
	this.selectedIndex = 0;
	this.selectLast = 0;
	this.currentModel = "";
	this:RegisterEvent("CHARACTER_LIST_UPDATE");
	this:RegisterEvent("UPDATE_SELECTED_CHARACTER");
	this:RegisterEvent("SELECT_LAST_CHARACTER");
	this:RegisterEvent("SELECT_FIRST_CHARACTER");
	this:RegisterEvent("SUGGEST_REALM");

	-- Hack to preload models...
	CharacterSelect:SetModel("Interface\\Glues\\Models\\UI_Scourge\\UI_Scourge.mdx");
	CharacterSelect:SetModel("Interface\\Glues\\Models\\UI_Dwarf\\UI_Dwarf.mdx");
	CharacterSelect:SetModel("Interface\\Glues\\Models\\UI_NightElf\\UI_NightElf.mdx");
	CharacterSelect:SetModel("Interface\\Glues\\Models\\UI_Tauren\\UI_Tauren.mdx");
	CharacterSelect:SetModel("Interface\\Glues\\Models\\UI_Human\\UI_Human.mdx");
	CharacterSelect:SetModel("Interface\\Glues\\Models\\UI_Orc\\UI_Orc.mdx");

--	CharacterSelect:ClearModel();
	local fogInfo = CharModelFogInfo["ORC"];
	CharacterSelect:SetFogColor(fogInfo.r, fogInfo.g, fogInfo.b);
	CharacterSelect:SetFogNear(0);
	CharacterSelect:SetFogFar(fogInfo.far);

	SetCharSelectModelFrame("CharacterSelect");
	--CharSelectModel:SetLight(1, 0, 0, -0.707, -0.707, 0.7, 1.0, 1.0, 1.0, 0.8, 1.0, 1.0, 0.8);

	-- Color edit box backdrops
	local backdropColor = DEFAULT_TOOLTIP_COLOR;
	CharacterSelectCharacterFrame:SetBackdropBorderColor(backdropColor[1], backdropColor[2], backdropColor[3]);
	CharacterSelectCharacterFrame:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6], 0.85);
	
end

function CharacterSelect_OnShow()
	CurrentGlueMusic = "Sound\\Music\\GlueScreenMusic\\wow_main_theme.mp3";

	local serverName = GetServerName();
	local connected = IsConnectedToServer();
	if ( serverName ) then
		if( not connected ) then
			serverName = serverName.."\n("..TEXT(SERVER_DOWN)..")";
		end
		CharSelectRealmName:SetText(serverName);
		CharSelectRealmName:Show();
	else
		CharSelectRealmName:Hide();
	end

	if ( connected ) then
		GetCharacterListUpdate();
	else
		UpdateCharacterList();
	end

	local billingTimeLeft = GetBillingTimeRemaining();
	if ( billingTimeLeft > 0 and billingTimeLeft < 8 ) then
		if ( billingTimeLeft == 1) then
			BillingWarningText:SetText(TEXT(BILLING_TIME_LEFT_LAST_DAY));
		else
			BillingWarningText:SetText(format(TEXT(BILLING_TIME_LEFT_DAYS), billingTimeLeft));
		end
		BillingWarningFrame:Show();
	else
		BillingWarningFrame:Hide();
	end

	-- fadein the character select ui
	GlueFrameFadeIn(CharacterSelectUI, CHARACTER_SELECT_FADE_IN)
end

function CharacterSelect_OnKeyDown()
	if ( arg1 == "ESCAPE" ) then
		CharacterSelect_Exit();
	elseif ( arg1 == "ENTER" ) then
		CharacterSelect_EnterWorld();
	elseif ( arg1 == "PRINTSCREEN" ) then
		Screenshot();
	elseif ( arg1 == "UP" or arg1 == "LEFT" ) then
		local numChars = GetNumCharacters();
		if ( numChars > 1 ) then
			if ( this.selectedIndex > 1 ) then
				CharacterSelect_SelectCharacter(this.selectedIndex - 1);
			else
				CharacterSelect_SelectCharacter(numChars);
			end
		end
	elseif ( arg1 == "DOWN" or arg1 == "RIGHT" ) then
		local numChars = GetNumCharacters();
		if ( numChars > 1 ) then
			if ( this.selectedIndex < GetNumCharacters() ) then
				CharacterSelect_SelectCharacter(this.selectedIndex + 1);
			else
				CharacterSelect_SelectCharacter(1);
			end
		end
	end
end

function CharacterSelect_OnEvent()
	if ( event == "CHARACTER_LIST_UPDATE" ) then
		UpdateCharacterList();
		CharSelectCharacterName:SetText(GetCharacterInfo(this.selectedIndex));
	elseif ( event == "UPDATE_SELECTED_CHARACTER" ) then
		if ( arg1 == 0 ) then
			CharSelectCharacterName:SetText("");
		else
			CharSelectCharacterName:SetText(GetCharacterInfo(arg1));
			this.selectedIndex = arg1;
		end
		UpdateCharacterSelection();
	elseif ( event == "SELECT_LAST_CHARACTER" ) then
		this.selectLast = 1;
	elseif ( event == "SELECT_FIRST_CHARACTER" ) then
		CharacterSelect_SelectCharacter(1, 1);
	elseif ( event == "SUGGEST_REALM" ) then
		local name = GetRealmInfo(arg1, arg2);
		RealmWizard.suggestedRealmName = name;
		RealmWizard.suggestedCategory = arg1;
		RealmWizard.suggestedID = arg2;
		GlueDialog_Show("SUGGEST_REALM");
	end
end

function CharacterSelect_UpdateModel()
	UpdateSelectionCustomizationScene();
	this:AdvanceTime();
end

function UpdateCharacterSelection()
	for i=1, MAX_CHARACTERS_DISPLAYED, 1 do
		getglobal("CharSelectCharacterButton"..i):UnlockHighlight();
	end

	local index = this.selectedIndex;
	if ( (index > 0) and (index <= MAX_CHARACTERS_DISPLAYED) )then
		getglobal("CharSelectCharacterButton"..index):LockHighlight();
	end
end

function UpdateCharacterList()
	local numChars = GetNumCharacters();
	local index = 1;
	local coords;
	for i=1, numChars, 1 do
		local name, race, class, level, zone, fileString, gender = GetCharacterInfo(i);
		if ( gender == 0 ) then
			gender = "MALE";
		else
			gender = "FEMALE";
		end
		local button = getglobal("CharSelectCharacterButton"..index);
		if ( not name ) then
			button:SetText("ERROR - Tell Jeremy");
		else
			if ( not zone ) then
				zone = "";
			end
			getglobal("CharSelectCharacterButton"..index.."ButtonTextName"):SetText(name);
			getglobal("CharSelectCharacterButton"..index.."ButtonTextInfo"):SetText(format(TEXT(CHARACTER_SELECT_INFO), level, class));
			getglobal("CharSelectCharacterButton"..index.."ButtonTextLocation"):SetText(zone);
		end
		button:Show();

		index = index + 1;
		if ( index > MAX_CHARACTERS_DISPLAYED ) then
			break;
		end
	end

	if ( numChars == 0 ) then
		CharacterSelectDeleteButton:Disable();
		CharSelectEnterWorldButton:Disable();
	else
		CharacterSelectDeleteButton:Enable();
		CharSelectEnterWorldButton:Enable();
	end

	CharacterSelect.createIndex = 0;
	CharSelectCreateCharacterButton:Hide();	
	
	local connected = IsConnectedToServer();
	for i=index, MAX_CHARACTERS_DISPLAYED, 1 do
		local button = getglobal("CharSelectCharacterButton"..index);
		if ( (CharacterSelect.createIndex == 0) and (numChars < MAX_CHARACTERS_PER_REALM) ) then
			CharacterSelect.createIndex = index;
			if ( connected ) then
				--If can create characters position and show the create button
				CharSelectCreateCharacterButton:SetID(index);
				--CharSelectCreateCharacterButton:SetPoint("TOP", button:GetName(), "TOP", 0, -5);
				CharSelectCreateCharacterButton:Show();	
			end
		end
		button:Hide();
		index = index + 1;
	end

	if ( numChars == 0 ) then
		CharacterSelect.selectedIndex = 0;
		return;
	end

	if ( CharacterSelect.selectLast == 1 ) then
		CharacterSelect.selectLast = 0;
		CharacterSelect_SelectCharacter(numChars, 1);
		return;
	end

	if ( (CharacterSelect.selectedIndex == 0) or (CharacterSelect.selectedIndex > numChars) ) then
		CharacterSelect.selectedIndex = 1;
	end
	CharacterSelect_SelectCharacter(CharacterSelect.selectedIndex, 1);
end

function CharacterSelect_OnChar()
end

function CharacterSelectButton_OnClick()
	local id = this:GetID();
	if ( id ~= CharacterSelect.selectedIndex ) then
		CharacterSelect_SelectCharacter(id);
	end
end

function CharacterSelectButton_OnDoubleClick()
	local id = this:GetID();
	if ( id ~= CharacterSelect.selectedIndex ) then
		CharacterSelect_SelectCharacter(id);
	end
	CharacterSelect_EnterWorld();
end

function CharacterSelect_TabResize()
	local buttonMiddle = getglobal(this:GetName().."Middle");
	local buttonMiddleDisabled = getglobal(this:GetName().."MiddleDisabled");
	local width = this:GetTextWidth() - 8;
	local leftWidth = getglobal(this:GetName().."Left"):GetWidth();
	buttonMiddle:SetWidth(width);
	buttonMiddleDisabled:SetWidth(width);
	this:SetWidth(width + (2 * leftWidth));
end

function CharacterSelect_SelectCharacter(id, noCreate)
	if ( id == CharacterSelect.createIndex ) then
		if ( not noCreate ) then
			PlaySound("gsCharacterSelectionCreateNew");
			--ResetRaceSelect();
			--UpdateSelectedRace(nil);
			SetGlueScreen("charcreate");
		end
	else
		local name, race, class, level, zone, fileString = GetCharacterInfo(id);

		if ( fileString ~= CharacterSelect.currentModel ) then
			CharacterSelect.currentModel = fileString;
			SetBackgroundModel(CharacterSelect, fileString);
		end
		SelectCharacter(id);
	end
end

function CharacterDeleteDialog_OnShow()
	local name, race, class, level = GetCharacterInfo(CharacterSelect.selectedIndex);
	CharacterDeleteText1:SetText(format(TEXT(CONFIRM_CHAR_DELETE), name, level, class));
	CharacterDeleteBackground:SetHeight(16 + CharacterDeleteText1:GetHeight() + CharacterDeleteText2:GetHeight() + 23 + CharacterDeleteEditBox:GetHeight() + 8 + CharacterDeleteButton1:GetHeight() + 16);
	CharacterDeleteButton1:Disable();
end

function CharacterSelect_EnterWorld()
	PlaySound("gsCharacterSelectionEnterWorld");
	EnterWorld();
end

function CharacterSelect_Exit()
	PlaySound("gsCharacterSelectionExit");
	DisconnectFromServer();
	SetGlueScreen("login");
end

function CharacterSelect_AccountOptions()
	PlaySound("gsCharacterSelectionAcctOptions");
end

function CharacterSelect_TechSupport()
	PlaySound("gsCharacterSelectionAcctOptions");
	LaunchURL(TEXT(TECH_SUPPORT_URL));
end

function CharacterSelect_Delete()
	PlaySound("gsCharacterSelectionDelCharacter");
	if ( CharacterSelect.selectedIndex > 0 ) then
		CharacterDeleteDialog:Show();
	end
end

function CharacterSelect_ChangeRealm()
	PlaySound("gsLoginChangeRealmSelect");
	RequestRealmList(1);
end

function CharacterSelectFrame_OnMouseDown(button)
	if ( button == "LeftButton" ) then
		CHARACTER_SELECT_ROTATION_START_X = GetCursorPosition();
		CHARACTER_SELECT_INITIAL_FACING = GetCharacterSelectFacing();
	end
end

function CharacterSelectFrame_OnMouseUp(button)
	if ( button == "LeftButton" ) then
		CHARACTER_SELECT_ROTATION_START_X = nil
	end
end

function CharacterSelectFrame_OnUpdate()
	if ( CHARACTER_SELECT_ROTATION_START_X ) then
		local x = GetCursorPosition();
		local diff = (x - CHARACTER_SELECT_ROTATION_START_X) * CHARACTER_ROTATION_CONSTANT;
		CHARACTER_SELECT_ROTATION_START_X = GetCursorPosition();
		SetCharacterSelectFacing(GetCharacterSelectFacing() + diff);
	end
end

function CharacterSelectRotateRight_OnUpdate()
	if ( this:GetButtonState() == "PUSHED" ) then
		SetCharacterSelectFacing(GetCharacterSelectFacing() + CHARACTER_FACING_INCREMENT);
	end
end

function CharacterSelectRotateLeft_OnUpdate()
	if ( this:GetButtonState() == "PUSHED" ) then
		SetCharacterSelectFacing(GetCharacterSelectFacing() - CHARACTER_FACING_INCREMENT);
	end
end

function CharacterSelect_ManageAccount()
	PlaySound("gsCharacterSelectionAcctOptions");
	LaunchURL(TEXT(AUTH_NO_TIME_URL));
end