#ifndef COMPARE_WINDOW_H
#define COMPARE_WINDOW_H

#include <Window.h>

class BFilePanel;
class BTextControl;


/** The opener: pick a left and a right file, then Compare. */
class CompareWindow : public BWindow {
public:
								CompareWindow();
	virtual						~CompareWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Fill(const BMessage* message);
			void				_Browse(uint32 what, const char* title);
			void				_Compare();

			BTextControl*		fLeft;
			BTextControl*		fRight;
			BFilePanel*			fOpenPanel;
};

#endif
