#include <Application.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Path.h>

#include <string>

#include "AppMessages.h"
#include "CompareWindow.h"
#include "DiffModel.h"
#include "MergeWindow.h"
#include "RunProcess.h"


enum {
	kMsgPatchChosen		= 'chpt',
	kMsgTargetChosen	= 'chtg'
};


class App : public BApplication {
public:
	App()
		:
		BApplication("application/x-vnd.PatchWork"),
		fWindowCount(0),
		fPatchPanel(NULL),
		fTargetPanel(NULL)
	{
	}

	virtual ~App()
	{
		delete fPatchPanel;
		delete fTargetPanel;
	}

	virtual void ReadyToRun()
	{
		// Launch refs/argv arrive before this; open the opener only if they
		// did not produce a window.
		if (fWindowCount == 0)
			_ShowCompareWindow(NULL);
	}

	virtual void RefsReceived(BMessage* message)
	{
		entry_ref first;
		entry_ref second;
		if (message->FindRef("refs", 0, &first) == B_OK
			&& message->FindRef("refs", 1, &second) == B_OK
			&& !message->HasRef("refs", 2)) {
			_OpenPair(BPath(&first).Path(), BPath(&second).Path());
		} else
			_ShowCompareWindow(message);
	}

	virtual void ArgvReceived(int32 argc, char** argv)
	{
		// A second launch (B_SINGLE_LAUNCH) forwards its argv with its own cwd.
		const char* cwd = NULL;
		if (CurrentMessage() != NULL)
			CurrentMessage()->FindString("cwd", &cwd);

		BMessage refs(B_REFS_RECEIVED);
		for (int32 i = 1; i < argc; i++) {
			BPath path;
			if (argv[i][0] != '/' && cwd != NULL)
				path.SetTo(cwd, argv[i]);
			else
				path.SetTo(argv[i]);
			entry_ref ref;
			if (path.InitCheck() == B_OK
				&& get_ref_for_path(path.Path(), &ref) == B_OK)
				refs.AddRef("refs", &ref);
		}
		if (!refs.IsEmpty())
			RefsReceived(&refs);
	}

	virtual void AboutRequested()
	{
		ShowAlert("About PatchWork",
			"PatchWork 1.0\n\nCompare two files and merge them, "
			"FileMerge style.\nUses GNU diff and patch.", B_INFO_ALERT);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgOpenPair:
			{
				const char* left;
				const char* right;
				if (message->FindString("left", &left) == B_OK
					&& message->FindString("right", &right) == B_OK)
					_OpenPair(left, right);
				break;
			}

			case kMsgShowCompare:
				_ShowCompareWindow(NULL);
				break;

			case kMsgWindowClosed:
				// Not B_QUIT_ON_WINDOW_CLOSE: that would quit on the first.
				if (--fWindowCount == 0)
					PostMessage(B_QUIT_REQUESTED);
				break;

			case kMsgApplyPatch:
				_ShowOpenPanel(fPatchPanel, kMsgPatchChosen,
					"PatchWork: Choose patch file");
				break;

			case kMsgPatchChosen:
			{
				entry_ref ref;
				if (message->FindRef("refs", &ref) != B_OK)
					break;
				fPatchFile.SetTo(&ref);
				// A second panel: re-showing the first from its own reply
				// could race with it hiding itself.
				_ShowOpenPanel(fTargetPanel, kMsgTargetChosen,
					"PatchWork: Choose file to patch");
				break;
			}

			case kMsgTargetChosen:
			{
				entry_ref ref;
				if (message->FindRef("refs", &ref) == B_OK)
					_ApplyPatch(BPath(&ref));
				break;
			}

			default:
				BApplication::MessageReceived(message);
				break;
		}
	}

private:
	void _OpenPair(const char* left, const char* right)
	{
		Comparison comparison;
		std::string error;
		int status = CompareFiles(left, right, comparison, error);
		if (status == 0) {
			ShowAlert("PatchWork", "The files are identical.", B_INFO_ALERT);
			return;
		}
		if (status != 1) {
			if (error.empty())
				error = "Could not run diff.";
			ShowAlert("diff failed", error.c_str(), B_STOP_ALERT);
			return;
		}
		fWindowCount++;
		(new MergeWindow(BPath(left), BPath(right), comparison))->Show();
	}

	/** Brings up the one compare window, creating it if needed. */
	void _ShowCompareWindow(BMessage* refs)
	{
		if (!fCompareWindow.IsValid()) {
			fWindowCount++;
			CompareWindow* window = new CompareWindow();
			fCompareWindow = BMessenger(window);
			window->Show();
		}
		fCompareWindow.SendMessage(kMsgShowCompare);
		if (refs != NULL) {
			BMessage fill(*refs);
			fill.what = B_REFS_RECEIVED;
			fCompareWindow.SendMessage(&fill);
		}
	}

	void _ShowOpenPanel(BFilePanel*& panel, uint32 what, const char* title)
	{
		if (panel == NULL)
			panel = new BFilePanel(B_OPEN_PANEL, NULL, NULL, B_FILE_NODE,
				false);
		BMessage message(what);
		panel->SetMessage(&message);
		if (panel->Window()->Lock()) {
			panel->Window()->SetTitle(title);
			panel->Window()->Unlock();
		}
		panel->Show();
	}

	void _ApplyPatch(const BPath& target)
	{
		// -N: refuse an already-applied patch instead of offering to reverse
		// it, which without a terminal could silently undo the user's change.
		const char* argv[] = { "patch", "-N", target.Path(), fPatchFile.Path(),
			NULL };
		std::string out;
		std::string err;
		int status = RunProcess(argv, out, err);

		std::string text = out + err;
		// shortcut: long output is cut so the alert stays on screen; show it
		// in a scrolling window if users need the full log.
		if (text.size() > 2000)
			text = text.substr(0, 2000) + "\n…";
		if (text.empty())
			text = status == 0 ? "Patch applied." : "Could not run patch.";

		ShowAlert(status == 0 ? "Patch applied" : "Patch failed", text.c_str(),
			status == 0 ? B_INFO_ALERT : B_STOP_ALERT);
	}

	int32		fWindowCount;	// compare + merge windows still open
	BMessenger	fCompareWindow;
	BFilePanel*	fPatchPanel;
	BFilePanel*	fTargetPanel;
	BPath		fPatchFile;
};


int
main()
{
	App app;
	app.Run();
	return 0;
}
