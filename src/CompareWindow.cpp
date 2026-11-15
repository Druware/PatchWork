#include "CompareWindow.h"

#include <Application.h>
#include <Button.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Path.h>
#include <TextControl.h>

#include "AppMessages.h"


enum {
	kMsgBrowseLeft		= 'brlf',
	kMsgBrowseRight		= 'brrt',
	kMsgLeftChosen		= 'chlf',
	kMsgRightChosen		= 'chrt',
	kMsgCompare			= 'cmpr'
};


CompareWindow::CompareWindow()
	:
	BWindow(BRect(120, 120, 620, 260), "Compare Files", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_V_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* fileMenu = new BMenu("File");
	BMenuItem* item = new BMenuItem("Apply Patch…",
		new BMessage(kMsgApplyPatch));
	item->SetTarget(be_app);
	fileMenu->AddItem(item);
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("Close", new BMessage(B_QUIT_REQUESTED),
		'W'));
	item = new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q');
	item->SetTarget(be_app);
	fileMenu->AddItem(item);
	menuBar->AddItem(fileMenu);
	BMenu* helpMenu = new BMenu("Help");
	item = new BMenuItem("About PatchWork", new BMessage(B_ABOUT_REQUESTED));
	item->SetTarget(be_app);
	helpMenu->AddItem(item);
	menuBar->AddItem(helpMenu);

	fLeft = new BTextControl("left", "Left:", "", NULL);
	fRight = new BTextControl("right", "Right:", "", NULL);
	BButton* compare = new BButton("compare", "Compare",
		new BMessage(kMsgCompare));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.AddTextControl(fLeft, 0, 0)
			.Add(new BButton("browseLeft", "Browse…",
				new BMessage(kMsgBrowseLeft)), 2, 0)
			.AddTextControl(fRight, 0, 1)
			.Add(new BButton("browseRight", "Browse…",
				new BMessage(kMsgBrowseRight)), 2, 1)
			.Add(compare, 2, 2)
		.End();
	SetDefaultButton(compare);
	fLeft->MakeFocus(true);

	BMessenger messenger(this);
	fOpenPanel = new BFilePanel(B_OPEN_PANEL, &messenger, NULL, B_FILE_NODE,
		false);
}


CompareWindow::~CompareWindow()
{
	delete fOpenPanel;
}


void
CompareWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgBrowseLeft:
			_Browse(kMsgLeftChosen, "PatchWork: Choose left file");
			break;
		case kMsgBrowseRight:
			_Browse(kMsgRightChosen, "PatchWork: Choose right file");
			break;

		case kMsgLeftChosen:
		case kMsgRightChosen:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BTextControl* field = message->what == kMsgLeftChosen
					? fLeft : fRight;
				field->SetText(BPath(&ref).Path());
			}
			break;
		}

		case kMsgCompare:
			_Compare();
			break;

		case kMsgShowCompare:
			if (IsHidden())
				Show();
			Activate();
			break;

		case B_REFS_RECEIVED:
			_Fill(message);
			break;

		case B_SIMPLE_DATA:
			// files dropped from Tracker; text views pass refs up to us
			if (message->HasRef("refs"))
				_Fill(message);
			else
				BWindow::MessageReceived(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
CompareWindow::QuitRequested()
{
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


/** Two refs fill both fields; one fills the field it was dropped on, else
	the first empty one (left first), else the right. */
void
CompareWindow::_Fill(const BMessage* message)
{
	entry_ref first;
	entry_ref second;
	if (message->FindRef("refs", 0, &first) != B_OK)
		return;
	if (message->FindRef("refs", 1, &second) == B_OK) {
		fLeft->SetText(BPath(&first).Path());
		fRight->SetText(BPath(&second).Path());
		return;
	}

	BTextControl* field = fLeft->Text()[0] == '\0' ? fLeft : fRight;
	if (message->WasDropped()) {
		BPoint where = message->DropPoint();
		if (fLeft->Bounds().Contains(fLeft->ConvertFromScreen(where)))
			field = fLeft;
		else if (fRight->Bounds().Contains(fRight->ConvertFromScreen(where)))
			field = fRight;
	}
	field->SetText(BPath(&first).Path());
}


void
CompareWindow::_Browse(uint32 what, const char* title)
{
	BMessage message(what);
	fOpenPanel->SetMessage(&message);
	if (fOpenPanel->Window()->Lock()) {
		fOpenPanel->Window()->SetTitle(title);
		fOpenPanel->Window()->Unlock();
	}
	fOpenPanel->Show();
}


void
CompareWindow::_Compare()
{
	if (fLeft->Text()[0] == '\0' || fRight->Text()[0] == '\0') {
		ShowAlert("PatchWork", "Choose a left and a right file to compare.",
			B_WARNING_ALERT);
		return;
	}
	BMessage pair(kMsgOpenPair);
	pair.AddString("left", fLeft->Text());
	pair.AddString("right", fRight->Text());
	be_app->PostMessage(&pair);
}
