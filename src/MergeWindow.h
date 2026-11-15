#ifndef MERGE_WINDOW_H
#define MERGE_WINDOW_H

#include <Path.h>
#include <Window.h>

#include <string>
#include <vector>

#include "DiffModel.h"

class BFilePanel;
class BPopUpMenu;
class BStringView;
class BTextView;
class DiffTextView;
class GutterView;


/** FileMerge-style window: left and right panes, connector gutter, and an
	editable merge pane built from each difference's choice. */
class MergeWindow : public BWindow {
public:
								MergeWindow(const BPath& left,
									const BPath& right,
									const Comparison& comparison);
	virtual						~MergeWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

	// For the panes and the gutter.
			const std::vector<Hunk>& Hunks() const
									{ return fModel.hunks; }
			int					Selected() const { return fSelected; }
			DiffTextView*		LeftView() const { return fLeftView; }
			DiffTextView*		RightView() const { return fRightView; }
			void				PaneScrolled(DiffTextView* pane);

private:
			void				_Select(int index);
			void				_SetChoice(Choice choice, bool confirmed);
			void				_Regenerate();
			void				_UpdateStatus();
			std::string			_MergeText() const;
			void				_ShowSavePanel(uint32 what, const char* name);
			void				_SaveMerge(const BMessage* message);
			void				_SavePatch(const BMessage* message);

			BPath				fLeftPath;
			BPath				fRightPath;
			Comparison			fModel;
			int					fSelected;
			std::vector<int>	fMergeLines;	// merge line of each hunk
			std::string			fGenerated;		// merge text last generated
			std::string			fSaved;			// merge text last saved

			DiffTextView*		fLeftView;
			DiffTextView*		fRightView;
			GutterView*			fGutter;
			BTextView*			fMergeView;
			BStringView*		fStatus;
			BPopUpMenu*			fChoiceMenu;
			BFilePanel*			fSavePanel;
			bool				fSyncing;
			bool				fCloseAfterSave;
			bool				fForceClose;
};

#endif
