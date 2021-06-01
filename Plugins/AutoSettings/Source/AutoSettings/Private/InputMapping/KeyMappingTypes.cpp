// Copyright Sam Bonifacio. All Rights Reserved.

#include "InputMapping/KeyMappingTypes.h"
#include "Misc/AutoSettingsConfig.h"

FConfigActionKeyMapping::FConfigActionKeyMapping(FInputActionKeyMapping Base)
{
	ActionName = Base.ActionName;
	Key = Base.Key;
	bShift = Base.bShift;
	bCtrl = Base.bCtrl;
	bAlt = Base.bAlt;
	bCmd = Base.bCmd;
	KeyGroup = GetDefault<UAutoSettingsConfig>()->GetKeyGroup(Key);
}

FConfigAxisKeyMapping::FConfigAxisKeyMapping(FInputAxisKeyMapping Base)
{
	AxisName = Base.AxisName;
	Key = Base.Key;
	Scale = Base.Scale;
	KeyGroup = GetDefault<UAutoSettingsConfig>()->GetKeyGroup(Key);
}
