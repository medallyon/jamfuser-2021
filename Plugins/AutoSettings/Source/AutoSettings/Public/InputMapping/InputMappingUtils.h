// Copyright Sam Bonifacio. All Rights Reserved.
#pragma once

class FInputMappingUtils
{
public:

	// Checks if the given Player Controller is valid for input mapping purposes (is a local player)
	static bool IsValidPlayer(APlayerController* PlayerController, bool bLogError = false, const FString& ErrorContext = FString());
	
};
