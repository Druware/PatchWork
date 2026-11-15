#include "MergeWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <NodeInfo.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <Shape.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextView.h>

#include <algorithm>
#include <errno.h>
#include <fs_attr.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "AppMessages.h"
#include "RunProcess.h"


enum {
	kMsgSelect			= 'slct',	// "index"
	kMsgNext			= 'nxdf',
	kMsgPrevious		= 'pvdf',
	kMsgChoose			= 'chos',	// "choice"
	kMsgDiscardReply	= 'dscr',	// "index", "choice", "which"
	kMsgCloseReply		= 'clrp',	// "which"
	kMsgSaveMerge		= 'svmg',
	kMsgSavePatch		= 'svpt',
	kMsgMergeTarget		= 'mgtg',
	kMsgPatchTarget		= 'pttg'
};

static const char* kChoiceLabels[] = { "Left", "Right", "Both (Left First)",
	"Both (Right First)", "Neither" };
static const char* kChoiceArrows[] = { "←", "→", "↔", "↔", "✕" };


/** Shade for differences, picked against the document background so dark
	themes stay readable; drawn with B_OP_ALPHA over the text. */
static rgb_color
Tint(bool selected)
{
	rgb_color tint = ui_color(B_DOCUMENT_BACKGROUND_COLOR).IsLight()
		? make_color(40, 100, 220) : make_color(110, 170, 255);
	tint.alpha = selected ? 110 : 45;
	return tint;
}


/** Top of a line in view coordinates. Panes use one fixed font and no word
	wrap, so every line has the same height. */
static float
LineTop(const BTextView* view, int line)
{
	return view->PointAt(0).y + line * view->LineHeight(0);
}


static void
ScrollClamped(BTextView* view, float x, float y)
{
	float min;
	float max;
	if (BScrollBar* bar = view->ScrollBar(B_HORIZONTAL)) {
		bar->GetRange(&min, &max);
		x = std::max(min, std::min(max, x));
	}
	if (BScrollBar* bar = view->ScrollBar(B_VERTICAL)) {
		bar->GetRange(&min, &max);
		y = std::max(min, std::min(max, y));
	}
	view->ScrollTo(BPoint(roundf(x), roundf(y)));
}


/** Scrolls so the line sits a third of the way down. */
static void
ScrollToLine(BTextView* view, int line)
{
	ScrollClamped(view, view->Bounds().left,
		LineTop(view, line) - view->Bounds().Height() / 3);
}


static std::string
Join(const std::vector<std::string>& lines)
{
	std::string text;
	for (const std::string& line : lines)
		text += line;
	return text;
}


/**
 * Saves data to the file a save panel chose without risking the old file.
 *
 * The data goes to a temporary file in the same directory, which is renamed
 * over the target only once it is completely written; on failure the target
 * is untouched and the temporary file is removed. An existing target's mode
 * and attributes are carried over, and a symlink is followed, not replaced.
 *
 * @param message  The save panel's B_SAVE_REQUESTED-style message.
 * @param data     The bytes to save.
 * @param type     The MIME type to give the file.
 * @param keepType If true, an existing target's own type wins over @a type.
 * @return B_OK, or the error that stopped the save.
 */
static status_t
WriteSaveTarget(const BMessage* message, const std::string& data,
	const char* type, bool keepType)
{
	entry_ref directory;
	const char* name;
	if (message->FindRef("directory", &directory) != B_OK
		|| message->FindString("name", &name) != B_OK)
		return B_BAD_VALUE;

	BPath path(&directory);
	status_t status = path.Append(name);
	if (status != B_OK)
		return status;
	// Save through a symlink to the file it points at.
	status = BEntry(path.Path(), true).GetPath(&path);
	if (status != B_OK)
		return status;

	struct stat targetStat;
	bool exists = stat(path.Path(), &targetStat) == 0;
	// Renaming would sidestep a read-only file, which plain writing refused.
	if (exists && access(path.Path(), W_OK) != 0)
		return errno;

	BPath parent;
	status = path.GetParent(&parent);
	if (status != B_OK)
		return status;
	char tempPath[B_PATH_NAME_LENGTH];
	snprintf(tempPath, sizeof(tempPath), "%s/.%s.patchwork-%" B_PRIdBIGTIME,
		parent.Path(), path.Leaf(), system_time());

	BFile file;
	status = file.SetTo(tempPath, B_WRITE_ONLY | B_CREATE_FILE
		| B_FAIL_IF_EXISTS);
	if (status != B_OK)
		return status;

	ssize_t written = file.Write(data.data(), data.size());
	if (written < 0)
		status = written;
	else if ((size_t)written != data.size())
		status = B_IO_ERROR;

	if (status == B_OK && exists) {
		status = file.SetPermissions(targetStat.st_mode);
		BNode target(path.Path());
		if (status == B_OK)
			status = target.InitCheck();
		char attrName[B_ATTR_NAME_LENGTH];
		while (status == B_OK && target.GetNextAttrName(attrName) == B_OK) {
			attr_info info;
			status = target.GetAttrInfo(attrName, &info);
			if (status != B_OK)
				break;
			std::vector<char> buffer(info.size);
			ssize_t size = target.ReadAttr(attrName, info.type, 0,
				buffer.data(), buffer.size());
			if (size >= 0)
				size = file.WriteAttr(attrName, info.type, 0, buffer.data(),
					size);
			if (size < 0)
				status = size;
		}
	}

	if (status == B_OK) {
		BNodeInfo nodeInfo(&file);
		char existingType[B_MIME_TYPE_LENGTH];
		if (!keepType || nodeInfo.GetType(existingType) != B_OK)
			status = nodeInfo.SetType(type);
	}
	if (status == B_OK)
		status = file.Sync();
	if (status == B_OK && rename(tempPath, path.Path()) != 0)
		status = errno;
	if (status != B_OK)
		unlink(tempPath);
	return status;
}


// #pragma mark - DiffTextView


/** Read-only pane that shades its side of every difference. */
class DiffTextView : public BTextView {
public:
	DiffTextView(const char* name, MergeWindow* owner, bool isLeft,
		const std::string& text)
		:
		BTextView(name, be_fixed_font, NULL, B_WILL_DRAW),
		fOwner(owner),
		fIsLeft(isLeft)
	{
		rgb_color color = ui_color(B_DOCUMENT_TEXT_COLOR);
		SetFontAndColor(be_fixed_font, B_FONT_ALL, &color);
		MakeEditable(false);
		SetWordWrap(false);
		// Without this a BTextView's minimum height is its whole text, so a
		// long file would grow the window past the screen.
		SetExplicitMinSize(BSize(100, 60));
		SetText(text.c_str(), text.size());
	}

	using BTextView::ScrollTo;

	virtual void ScrollTo(BPoint where)
	{
		BTextView::ScrollTo(where);
		fOwner->PaneScrolled(this);
	}

	virtual void Draw(BRect update)
	{
		BTextView::Draw(update);
		PushState();
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		for (size_t i = 0; i < fOwner->Hunks().size(); i++) {
			BRect rect = _HunkRect(i);
			if (!rect.Intersects(update))
				continue;
			SetHighColor(Tint((int)i == fOwner->Selected()));
			FillRect(rect);
		}
		PopState();
	}

	virtual void MouseDown(BPoint where)
	{
		for (size_t i = 0; i < fOwner->Hunks().size(); i++) {
			BRect rect = _HunkRect(i).InsetByCopy(0, -2);
			if (where.y >= rect.top && where.y <= rect.bottom) {
				BMessage select(kMsgSelect);
				select.AddInt32("index", i);
				Window()->PostMessage(&select);
				return;
			}
		}
		BTextView::MouseDown(where);
	}

private:
	/** This side's lines of a hunk; a thin bar where the other side has
		lines but this one has none. */
	BRect _HunkRect(size_t index) const
	{
		const Hunk& hunk = fOwner->Hunks()[index];
		int start = fIsLeft ? hunk.leftStart : hunk.rightStart;
		int count = fIsLeft ? hunk.leftCount : hunk.rightCount;
		float top = LineTop(this, start);
		float bottom = LineTop(this, start + count) - 1;
		if (count == 0) {
			top -= 1;
			bottom = top + 2;
		}
		return BRect(Bounds().left, top, Bounds().right, bottom);
	}

	MergeWindow*	fOwner;
	bool			fIsLeft;
};


// #pragma mark - GutterView


/** Center strip: a band from each difference's left lines to its right
	lines, with an arrow showing the choice. */
class GutterView : public BView {
public:
	GutterView(MergeWindow* owner)
		:
		BView("gutter", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fOwner(owner)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(44, B_SIZE_UNSET));
		SetExplicitMaxSize(BSize(44, B_SIZE_UNLIMITED));
		BFont font(be_bold_font);
		font.SetSize(15);
		SetFont(&font);
	}

	virtual void Draw(BRect update)
	{
		float width = Bounds().Width();
		font_height height;
		GetFontHeight(&height);
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		for (size_t i = 0; i < fOwner->Hunks().size(); i++) {
			float lt, lb, rt, rb;
			_Band(i, lt, lb, rt, rb);
			if (std::max(lb, rb) < update.top
				|| std::min(lt, rt) > update.bottom)
				continue;

			BShape shape;
			shape.MoveTo(BPoint(0, lt));
			shape.BezierTo(BPoint(width / 2, lt), BPoint(width / 2, rt),
				BPoint(width, rt));
			shape.LineTo(BPoint(width, rb));
			shape.BezierTo(BPoint(width / 2, rb), BPoint(width / 2, lb),
				BPoint(0, lb));
			shape.Close();

			rgb_color tint = Tint((int)i == fOwner->Selected());
			MovePenTo(B_ORIGIN);
			SetHighColor(tint);
			FillShape(&shape);
			tint.alpha = 200;
			SetHighColor(tint);
			StrokeShape(&shape);

			const char* arrow = kChoiceArrows[fOwner->Hunks()[i].choice];
			float middle = (lt + lb + rt + rb) / 4;
			SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
			DrawString(arrow, BPoint((width - StringWidth(arrow)) / 2,
				middle + (height.ascent - height.descent) / 2));
		}
	}

	virtual void MouseDown(BPoint where)
	{
		// shortcut: hit-tests the straight line between the band's ends, not
		// the curve; close enough for a 44 pixel wide strip.
		float t = where.x / std::max(1.0f, Bounds().Width());
		for (size_t i = 0; i < fOwner->Hunks().size(); i++) {
			float lt, lb, rt, rb;
			_Band(i, lt, lb, rt, rb);
			if (where.y >= lt + (rt - lt) * t - 3
				&& where.y <= lb + (rb - lb) * t + 3) {
				BMessage select(kMsgSelect);
				select.AddInt32("index", i);
				Window()->PostMessage(&select);
				return;
			}
		}
	}

private:
	float _Y(const BView* pane, float y) const
	{
		return ConvertFromScreen(pane->ConvertToScreen(BPoint(0, y))).y;
	}

	/** The band's ends in this view: left top/bottom, right top/bottom. */
	void _Band(size_t index, float& lt, float& lb, float& rt, float& rb) const
	{
		const Hunk& hunk = fOwner->Hunks()[index];
		const BTextView* left = fOwner->LeftView();
		const BTextView* right = fOwner->RightView();
		lt = _Y(left, LineTop(left, hunk.leftStart));
		lb = _Y(left, LineTop(left, hunk.leftStart + hunk.leftCount));
		rt = _Y(right, LineTop(right, hunk.rightStart));
		rb = _Y(right, LineTop(right, hunk.rightStart + hunk.rightCount));
	}

	MergeWindow*	fOwner;
};


// #pragma mark - MergeWindow


static BRect
NextFrame()
{
	static int sCount = 0;
	float offset = 24 * (sCount++ % 8);
	return BRect(80, 60, 1000, 760).OffsetByCopy(offset, offset);
}


MergeWindow::MergeWindow(const BPath& left, const BPath& right,
	const Comparison& comparison)
	:
	BWindow(NextFrame(), "PatchWork", B_DOCUMENT_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	fLeftPath(left),
	fRightPath(right),
	fModel(comparison),
	fSelected(0),
	fLeftView(NULL),
	fRightView(NULL),
	fGutter(NULL),
	fMergeView(NULL),
	fSyncing(false),
	fCloseAfterSave(false),
	fForceClose(false)
{
	SetTitle((std::string(left.Leaf()) + " ↔ " + right.Leaf()).c_str());

	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* menu = new BMenu("File");
	BMenuItem* item = new BMenuItem("Compare Files…",
		new BMessage(kMsgShowCompare), 'N');
	item->SetTarget(be_app);
	menu->AddItem(item);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Save Merge…", new BMessage(kMsgSaveMerge),
		'S'));
	menu->AddItem(new BMenuItem("Save Patch…", new BMessage(kMsgSavePatch)));
	item = new BMenuItem("Apply Patch…", new BMessage(kMsgApplyPatch));
	item->SetTarget(be_app);
	menu->AddItem(item);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Close", new BMessage(B_QUIT_REQUESTED), 'W'));
	item = new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q');
	item->SetTarget(be_app);
	menu->AddItem(item);
	menuBar->AddItem(menu);

	menu = new BMenu("Actions");
	menu->AddItem(new BMenuItem("Next Difference", new BMessage(kMsgNext),
		B_DOWN_ARROW));
	menu->AddItem(new BMenuItem("Previous Difference",
		new BMessage(kMsgPrevious), B_UP_ARROW));
	menu->AddSeparatorItem();
	fChoiceMenu = new BPopUpMenu("choice");
	const char shortcuts[] = { B_LEFT_ARROW, B_RIGHT_ARROW, 0, 0, 0 };
	for (int32 choice = kChooseLeft; choice <= kChooseNeither; choice++) {
		BMessage* message = new BMessage(kMsgChoose);
		message->AddInt32("choice", choice);
		std::string label = std::string("Choose ") + kChoiceLabels[choice];
		menu->AddItem(new BMenuItem(label.c_str(), message, shortcuts[choice]));
		fChoiceMenu->AddItem(new BMenuItem(kChoiceLabels[choice],
			new BMessage(*message)));
	}
	menuBar->AddItem(menu);

	menu = new BMenu("Help");
	item = new BMenuItem("About PatchWork", new BMessage(B_ABOUT_REQUESTED));
	item->SetTarget(be_app);
	menu->AddItem(item);
	menuBar->AddItem(menu);

	fLeftView = new DiffTextView("left", this, true, Join(fModel.left));
	fRightView = new DiffTextView("right", this, false, Join(fModel.right));
	fGutter = new GutterView(this);
	rgb_color textColor = ui_color(B_DOCUMENT_TEXT_COLOR);
	fMergeView = new BTextView("merge", be_fixed_font, &textColor,
		B_WILL_DRAW);
	fMergeView->SetWordWrap(false);
	fMergeView->SetExplicitMinSize(BSize(100, 60));

	fStatus = new BStringView("status", "");
	BMenuField* choiceField = new BMenuField("choiceField", "Actions:",
		fChoiceMenu);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddSplit(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_SMALL_INSETS, B_USE_SMALL_INSETS,
				B_USE_SMALL_INSETS, 0)
			.AddGroup(B_HORIZONTAL, 0, 3)
				.Add(new BScrollView("leftScroll", fLeftView, 0, true, true))
				.Add(fGutter)
				.Add(new BScrollView("rightScroll", fRightView, 0, true, true))
			.End()
			.Add(new BScrollView("mergeScroll", fMergeView, 0, true, true), 2)
		.End()
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_SMALL_INSETS)
			.Add(fStatus)
			.AddGlue()
			.Add(choiceField)
		.End();

	BMessenger messenger(this);
	fSavePanel = new BFilePanel(B_SAVE_PANEL, &messenger);

	_Regenerate();
	fSaved = fGenerated;	// nothing to lose until the user changes something
	// Posted, so it runs after Show() has laid out the panes.
	BMessage select(kMsgSelect);
	select.AddInt32("index", 0);
	PostMessage(&select);
}


MergeWindow::~MergeWindow()
{
	delete fSavePanel;
}


void
MergeWindow::MessageReceived(BMessage* message)
{
	int32 count = fModel.hunks.size();
	switch (message->what) {
		case kMsgSelect:
		{
			int32 index;
			if (message->FindInt32("index", &index) == B_OK && index >= 0
				&& index < count)
				_Select(index);
			break;
		}
		case kMsgNext:
			if (fSelected + 1 < count)
				_Select(fSelected + 1);
			break;
		case kMsgPrevious:
			if (fSelected > 0)
				_Select(fSelected - 1);
			break;

		case kMsgChoose:
		{
			int32 choice;
			if (message->FindInt32("choice", &choice) == B_OK
				&& choice >= kChooseLeft && choice <= kChooseNeither)
				_SetChoice((Choice)choice, false);
			break;
		}

		case kMsgDiscardReply:
		{
			int32 which;
			int32 index;
			int32 choice;
			if (message->FindInt32("which", &which) == B_OK && which == 1
				&& message->FindInt32("index", &index) == B_OK
				&& message->FindInt32("choice", &choice) == B_OK
				&& index >= 0 && index < count) {
				_Select(index);
				_SetChoice((Choice)choice, true);
			}
			break;
		}

		case kMsgCloseReply:
		{
			int32 which;
			if (message->FindInt32("which", &which) != B_OK)
				break;
			if (which == 1) {
				fForceClose = true;
				PostMessage(B_QUIT_REQUESTED);
			} else if (which == 2) {
				fCloseAfterSave = true;
				_ShowSavePanel(kMsgMergeTarget, fRightPath.Leaf());
			}
			break;
		}

		case kMsgSaveMerge:
			fCloseAfterSave = false;
			_ShowSavePanel(kMsgMergeTarget, fRightPath.Leaf());
			break;
		case kMsgSavePatch:
			_ShowSavePanel(kMsgPatchTarget,
				(std::string(fRightPath.Leaf()) + ".patch").c_str());
			break;
		case kMsgMergeTarget:
			_SaveMerge(message);
			break;
		case kMsgPatchTarget:
			_SavePatch(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
MergeWindow::QuitRequested()
{
	if (fForceClose || _MergeText() == fSaved) {
		be_app->PostMessage(kMsgWindowClosed);
		return true;
	}

	std::string text = std::string("Save the merge of ") + Title()
		+ " before closing?";
	BAlert* alert = new BAlert("Close", text.c_str(), "Cancel", "Don't Save",
		"Save", B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	alert->Go(new BInvoker(new BMessage(kMsgCloseReply), this));
	return false;
}


/** Keeps the other pane aligned through the hunk mapping. */
void
MergeWindow::PaneScrolled(DiffTextView* pane)
{
	if (fGutter == NULL)
		return;		// still constructing
	fGutter->Invalidate();
	if (fSyncing)
		return;

	fSyncing = true;
	DiffTextView* other = pane == fLeftView ? fRightView : fLeftView;
	double top = (pane->Bounds().top - LineTop(pane, 0)) / pane->LineHeight(0);
	double mapped = MapLine(fModel.hunks, top, pane == fLeftView);
	ScrollClamped(other, pane->Bounds().left,
		LineTop(other, 0) + mapped * other->LineHeight(0));
	fSyncing = false;
}


void
MergeWindow::_Select(int index)
{
	fSelected = index;
	_UpdateStatus();

	const Hunk& hunk = fModel.hunks[index];
	fSyncing = true;	// each pane goes to its own side of the hunk
	ScrollToLine(fLeftView, hunk.leftStart);
	ScrollToLine(fRightView, hunk.rightStart);
	ScrollToLine(fMergeView, fMergeLines[index]);
	fSyncing = false;
	fLeftView->Invalidate();
	fRightView->Invalidate();
	fGutter->Invalidate();
}


/** Applies a choice to the selected difference. Unless confirmed, asks
	first when the merge pane holds hand edits that regenerating would lose. */
void
MergeWindow::_SetChoice(Choice choice, bool confirmed)
{
	if (fModel.hunks[fSelected].choice == choice)
		return;

	if (!confirmed && _MergeText() != fGenerated) {
		BMessage* reply = new BMessage(kMsgDiscardReply);
		reply->AddInt32("index", fSelected);
		reply->AddInt32("choice", choice);
		BAlert* alert = new BAlert("Discard",
			"Discard your edits in the merge pane?", "Cancel", "Discard", NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		alert->Go(new BInvoker(reply, this));
		_UpdateStatus();	// the popup shows the old choice until confirmed
		return;
	}

	fModel.hunks[fSelected].choice = choice;
	_Regenerate();
	_Select(fSelected);
}


void
MergeWindow::_Regenerate()
{
	fGenerated = BuildMerge(fModel, &fMergeLines);
	fMergeView->SetText(fGenerated.c_str(), fGenerated.size());
}


void
MergeWindow::_UpdateStatus()
{
	char text[64];
	snprintf(text, sizeof(text), "Difference %d of %d", fSelected + 1,
		(int)fModel.hunks.size());
	fStatus->SetText(text);
	BMenuItem* item = fChoiceMenu->ItemAt(fModel.hunks[fSelected].choice);
	if (item != NULL)
		item->SetMarked(true);
}


std::string
MergeWindow::_MergeText() const
{
	return std::string(fMergeView->Text(), fMergeView->TextLength());
}


void
MergeWindow::_ShowSavePanel(uint32 what, const char* name)
{
	BMessage message(what);
	fSavePanel->SetMessage(&message);
	BPath directory;
	if (fRightPath.GetParent(&directory) == B_OK)
		fSavePanel->SetPanelDirectory(directory.Path());
	fSavePanel->SetSaveText(name);
	fSavePanel->Show();
}


void
MergeWindow::_SaveMerge(const BMessage* message)
{
	std::string text = _MergeText();
	// Keep an overwritten file's type, else take the right file's.
	BNode rightNode(fRightPath.Path());
	BNodeInfo rightInfo(&rightNode);
	char type[B_MIME_TYPE_LENGTH];
	if (rightInfo.GetType(type) != B_OK)
		strlcpy(type, "text/plain", sizeof(type));
	status_t status = WriteSaveTarget(message, text, type, true);
	if (status != B_OK) {
		fCloseAfterSave = false;
		ShowAlert("Save failed", strerror(status), B_STOP_ALERT);
		return;
	}
	fSaved = text;
	if (fCloseAfterSave)
		PostMessage(B_QUIT_REQUESTED);
}


void
MergeWindow::_SavePatch(const BMessage* message)
{
	const char* argv[] = { "diff", "-u", "--", fLeftPath.Path(),
		fRightPath.Path(), NULL };
	std::string out;
	std::string err;
	int result = RunProcess(argv, out, err);
	if (result == 0) {
		ShowAlert("PatchWork", "The files are now identical; there is no "
			"patch to save.", B_INFO_ALERT);
		return;
	}
	if (result != 1) {
		ShowAlert("diff failed", err.empty() ? "Could not run diff."
			: err.c_str(), B_STOP_ALERT);
		return;
	}

	status_t status = WriteSaveTarget(message, out, "text/x-patch", false);
	if (status != B_OK)
		ShowAlert("Save failed", strerror(status), B_STOP_ALERT);
}
