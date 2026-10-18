#pragma once

namespace site
{
	// Connects to the control panel and applies the account's settings to the
	// live cheat. Runs on its own thread (see guarded_thread in main.cpp).
	void run();
}
