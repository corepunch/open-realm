SECONDS_PER_PULSE = 0.75;

function GlueButtonMaster_OnUpdate(elapsed)
	if ( getglobal(this:GetName().."Rays"):IsVisible() ) then
		local SECONDS_PER_PULSE = 0.75;
		local sign = this.pulseSign;
		local counter;
		
		if ( this.startPulse ) then
			counter = 0;
			this.startPulse = nil;
			sign = 1;
		else
			counter = this.pulseCounter + (sign * elapsed);
			if ( counter > SECONDS_PER_PULSE ) then
				counter = SECONDS_PER_PULSE;
				sign = -sign;
			elseif ( counter < 0) then
				counter = 0;
				sign = -sign;
			end
		end
		
		local alpha = counter / SECONDS_PER_PULSE;
		getglobal(this:GetName().."Rays"):SetVertexColor(1.0, 1.0, 1.0, alpha);

		this.pulseSign = sign;
		this.pulseCounter = counter;
	end
end
