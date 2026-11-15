#pragma once
#include <mutex>

struct ImFont;

class OverlayBase
{
public:
	virtual ~OverlayBase() = default;
	static OverlayBase* Instance;

	void HookReady();
	void SetupFonts();
	void OverlayProc();

	void SetupOverlay();
	bool IsReady() const { return bIsReady; }

	ImFont* FontDefault; // Inter-Medium
	ImFont* FontHUD;
	ImFont* InterBlack;
	ImFont* InterLight;
	ImFont* FontAwesome;
	ImFont* ESPFont;

protected:
	// Called always - use to draw an HUD
	virtual void DrawHUD() = 0;
	// Called to display the FOV circle
	virtual void DrawGuiESP() = 0;
	// Called to display the ESP logic
	virtual void DrawGuiFOVCircle() = 0;

protected:
	std::recursive_mutex OverlayMutex;
	bool bSetupOverlayCalled;
	bool bIsReady;
};
