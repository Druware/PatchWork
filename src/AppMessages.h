#ifndef APP_MESSAGES_H
#define APP_MESSAGES_H

#include <Alert.h>

/** Messages handled by the application (sent from any window). */
enum {
	kMsgOpenPair		= 'oppr',	// "left", "right": paths to compare
	kMsgShowCompare		= 'shcm',	// optional "refs" to fill in
	kMsgWindowClosed	= 'wncl',
	kMsgApplyPatch		= 'appt'
};


static inline void
ShowAlert(const char* title, const char* text, alert_type type)
{
	BAlert* alert = new BAlert(title, text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, type);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}

#endif
