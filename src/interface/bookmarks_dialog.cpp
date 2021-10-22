#include "filezilla.h"
#include "bookmarks_dialog.h"
#include "filezillaapp.h"
#include "sitemanager.h"
#include "state.h"
#include "themeprovider.h"
#include "treectrlex.h"
#include "xmlfunctions.h"
#include "xrc_helper.h"

#include "../commonui/ipcmutex.h"

#include <wx/dirdlg.h>
#include <wx/statline.h>

BEGIN_EVENT_TABLE(CNewBookmarkDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CNewBookmarkDialog::OnOK)
EVT_BUTTON(XRCID("ID_BROWSE"), CNewBookmarkDialog::OnBrowse)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(CBookmarksDialog, wxDialogEx)
EVT_TREE_SEL_CHANGING(XRCID("ID_TREE"), CBookmarksDialog::OnSelChanging)
EVT_TREE_SEL_CHANGED(XRCID("ID_TREE"), CBookmarksDialog::OnSelChanged)
EVT_BUTTON(XRCID("wxID_OK"), CBookmarksDialog::OnOK)
EVT_BUTTON(XRCID("ID_BOOKMARK_BROWSE"), CBookmarksDialog::OnBrowse)
EVT_BUTTON(XRCID("ID_NEW"), CBookmarksDialog::OnNewBookmark)
EVT_BUTTON(XRCID("ID_RENAME"), CBookmarksDialog::OnRename)
EVT_BUTTON(XRCID("ID_DELETE"), CBookmarksDialog::OnDelete)
EVT_BUTTON(XRCID("ID_COPY"), CBookmarksDialog::OnCopy)
EVT_TREE_BEGIN_LABEL_EDIT(XRCID("ID_TREE"), CBookmarksDialog::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(XRCID("ID_TREE"), CBookmarksDialog::OnEndLabelEdit)
END_EVENT_TABLE()

CNewBookmarkDialog::CNewBookmarkDialog(wxWindow* parent, std::wstring& site_path, Site const* site)
	: m_parent(parent)
	, m_site_path(site_path)
	, site_(site)
{
}

int CNewBookmarkDialog::Run(wxString const& local_path, CServerPath const& remote_path)
{
	if (!Load(m_parent, _T("ID_NEWBOOKMARK"))) {
		return wxID_CANCEL;
	}

	xrc_call(*this, "ID_LOCALPATH", &wxTextCtrl::ChangeValue, local_path);
	if (!remote_path.empty()) {
		xrc_call(*this, "ID_REMOTEPATH", &wxTextCtrl::ChangeValue, remote_path.GetPath());
	}

	if (!site_) {
		xrc_call(*this, "ID_TYPE_SITE", &wxRadioButton::Enable, false);
	}

	return ShowModal();
}

void CNewBookmarkDialog::OnOK(wxCommandEvent&)
{
	bool const global = xrc_call(*this, "ID_TYPE_GLOBAL", &wxRadioButton::GetValue);

	wxString const name = xrc_call(*this, "ID_NAME", &wxTextCtrl::GetValue);
	if (name.empty()) {
		wxMessageBoxEx(_("You need to enter a name for the bookmark."), _("New bookmark"), wxICON_EXCLAMATION, this);
		return;
	}

	wxString const local_path = xrc_call(*this, "ID_LOCALPATH", &wxTextCtrl::GetValue);
	wxString remote_path_raw = xrc_call(*this, "ID_REMOTEPATH", &wxTextCtrl::GetValue);

	CServerPath remote_path;
	if (!remote_path_raw.empty()) {
		if (!global && site_) {
			remote_path.SetType(site_->GetOriginalServer().GetType());
		}
		if (!remote_path.SetPath(remote_path_raw.ToStdWstring())) {
			wxMessageBoxEx(_("Could not parse remote path."), _("New bookmark"), wxICON_EXCLAMATION);
			return;
		}
	}

	if (local_path.empty() && remote_path_raw.empty()) {
		wxMessageBoxEx(_("You need to enter at least one path, empty bookmarks are not supported."), _("New bookmark"), wxICON_EXCLAMATION, this);
		return;
	}

	bool const sync = xrc_call(*this, "ID_SYNC", &wxCheckBox::GetValue);
	if (sync && (local_path.empty() || remote_path_raw.empty())) {
		wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this bookmark."), _("New bookmark"), wxICON_EXCLAMATION, this);
		return;
	}

	bool const comparison = xrc_call(*this, "ID_COMPARISON", &wxCheckBox::GetValue);

	if (!global && site_) {
		std::unique_ptr<Site> site;
		if (!m_site_path.empty()) {
			site = CSiteManager::GetSiteByPath(m_site_path).first;
		}
		if (!site) {
			if (wxMessageBoxEx(_("Site-specific bookmarks require the server to be stored in the Site Manager.\nAdd current connection to the site manager?"), _("New bookmark"), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
				return;
			}

			m_site_path = CSiteManager::AddServer(*site_);
			if (m_site_path.empty()) {
				wxMessageBoxEx(_("Could not add connection to Site Manager"), _("New bookmark"), wxICON_EXCLAMATION, this);
				EndModal(wxID_CANCEL);
				return;
			}
		}
		else {
			for (auto const& bookmark : site->m_bookmarks) {
				if (bookmark.m_name == name) {
					wxMessageBoxEx(_("A bookmark with the entered name already exists. Please enter an unused name."), _("New bookmark"), wxICON_EXCLAMATION, this);
					return;
				}
			}
		}

		CSiteManager::AddBookmark(m_site_path, name, local_path, remote_path, sync, comparison);

		EndModal(wxID_OK);
	}
	else {
		if (!CBookmarksDialog::AddBookmark(name, local_path, remote_path, sync, comparison)) {
			return;
		}

		EndModal(wxID_OK);
	}
}

void CNewBookmarkDialog::OnBrowse(wxCommandEvent&)
{
	wxTextCtrl *const pText = XRCCTRL(*this, "ID_LOCALPATH", wxTextCtrl);

	wxDirDialog dlg(this, _("Choose the local directory"), pText->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK) {
		pText->ChangeValue(dlg.GetPath());
	}
}

class CBookmarkItemData : public wxTreeItemData
{
public:
	CBookmarkItemData()
	{
	}

	CBookmarkItemData(std::wstring const& local_dir, const CServerPath& remote_dir, bool sync, bool comparison)
		: m_local_dir(local_dir), m_remote_dir(remote_dir), m_sync(sync)
		, m_comparison(comparison)
	{
	}

	std::wstring m_local_dir;
	CServerPath m_remote_dir;
	bool m_sync{};
	bool m_comparison{};
};

CBookmarksDialog::CBookmarksDialog(wxWindow* parent, std::wstring& site_path, Site const* site)
	: m_parent(parent)
	, m_site_path(site_path)
	, site_(site)
{
}

void CBookmarksDialog::LoadGlobalBookmarks()
{
	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	for (auto bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		wxString name;
		std::wstring local_dir;
		std::wstring remote_dir_raw;
		CServerPath remote_dir;

		name = GetTextElement(bookmark, "Name");
		if (name.empty()) {
			continue;
		}

		local_dir = GetTextElement(bookmark, "LocalDir");
		remote_dir_raw = GetTextElement(bookmark, "RemoteDir");
		if (!remote_dir_raw.empty()) {
			if (!remote_dir.SetSafePath(remote_dir_raw)) {
				continue;
			}
		}
		if (local_dir.empty() && remote_dir.empty()) {
			continue;
		}

		bool sync;
		if (local_dir.empty() || remote_dir.empty()) {
			sync = false;
		}
		else {
			sync = GetTextElementBool(bookmark, "SyncBrowsing");
		}

		bool const comparison = GetTextElementBool(bookmark, "DirectoryComparison");

		CBookmarkItemData *data = new CBookmarkItemData(local_dir, remote_dir, sync, comparison);
		tree_->AppendItem(m_bookmarks_global, name, 1, 1, data);
	}

	tree_->SortChildren(m_bookmarks_global);
}

void CBookmarksDialog::LoadSiteSpecificBookmarks()
{
	if (m_site_path.empty()) {
		return;
	}

	auto const site = CSiteManager::GetSiteByPath(m_site_path).first;
	if (!site) {
		return;
	}

	for (auto const& bookmark : site->m_bookmarks) {
		CBookmarkItemData* new_data = new CBookmarkItemData(bookmark.m_localDir, bookmark.m_remoteDir, bookmark.m_sync, bookmark.m_comparison);
		tree_->AppendItem(m_bookmarks_site, bookmark.m_name, 1, 1, new_data);
	}

	tree_->SortChildren(m_bookmarks_site);
}

wxPanel* CreateBookmarkPanel(wxWindow* parent, DialogLayout const& lay);

int CBookmarksDialog::Run()
{
	SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	if (!wxDialogEx::Create(m_parent, -1, _("Bookmarks"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxSYSTEM_MENU | wxRESIZE_BORDER | wxCLOSE_BOX)) {
		return false;
	}

	auto const& lay = layout();

	auto main = lay.createMain(this, 1);
	main->AddGrowableCol(0);
	main->AddGrowableRow(0);

	auto sides = new wxBoxSizer(wxHORIZONTAL);
	main->Add(sides, lay.grow)->SetProportion(1);

	auto left = lay.createFlex(1);
	left->AddGrowableCol(0);
	left->AddGrowableRow(1);
	sides->Add(left, lay.grow)->SetProportion(1);

	left->Add(new wxStaticText(this, wxID_ANY, _("Bookmark:")));

	tree_ = new wxTreeCtrlEx(this, XRCID("ID_TREE"), wxDefaultPosition, wxDefaultSize, DEFAULT_TREE_STYLE | wxBORDER_SUNKEN | wxTR_EDIT_LABELS | wxTR_HIDE_ROOT);
	tree_->SetFocus();
	tree_->SetMinSize(wxSize(-1, 250));
	left->Add(tree_, lay.grow)->SetProportion(1);

	auto entrybuttons = new wxGridSizer(2, wxSize(lay.gap, lay.gap));
	left->Add(entrybuttons, lay.halign);

	entrybuttons->Add(new wxButton(this, XRCID("ID_NEW"), _("New Book&mark")), lay.grow);
	entrybuttons->Add(new wxButton(this, XRCID("ID_RENAME"), _("&Rename")), lay.grow);
	entrybuttons->Add(new wxButton(this, XRCID("ID_DELETE"), _("&Delete")), lay.grow);
	entrybuttons->Add(new wxButton(this, XRCID("ID_COPY"), _("D&uplicate")), lay.grow);

	auto buttons = lay.createButtonSizer(this, main, true);

	auto ok = new wxButton(this, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	wxSize s = CThemeProvider::GetIconSize(iconSizeSmall);
	wxImageList* pImageList = new wxImageList(s.x, s.y);

	pImageList->Add(wxArtProvider::GetBitmap(_T("ART_FOLDER"), wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall)));
	pImageList->Add(wxArtProvider::GetBitmap(_T("ART_BOOKMARK"), wxART_OTHER, CThemeProvider::GetIconSize(iconSizeSmall)));

	tree_->AssignImageList(pImageList);

	auto right = new wxBoxSizer(wxVERTICAL);
	sides->Add(right, 0, wxLEFT | wxGROW, lay.gap);

	wxNotebook* notebook = new wxNotebook(this, XRCID("ID_NOTEBOOK"));
	right->Add(notebook, 1, wxGROW);

	wxTreeItemId root = tree_->AddRoot(wxString());
	m_bookmarks_global = tree_->AppendItem(root, _("Global bookmarks"), 0, 0);
	LoadGlobalBookmarks();
	tree_->Expand(m_bookmarks_global);
	if (site_) {
		m_bookmarks_site = tree_->AppendItem(root, _("Site-specific bookmarks"), 0, 0);
		LoadSiteSpecificBookmarks();
		tree_->Expand(m_bookmarks_site);
	}

	auto * pPanel = CreateBookmarkPanel(notebook, layout());
	notebook->AddPage(pPanel, _("Bookmark"));

	xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::GetContainingSizer)->GetItem((size_t)0)->SetMinSize(200, -1);

	GetSizer()->Fit(this);

	wxSize minSize = GetSizer()->GetMinSize();
	wxSize size = GetSize();
	wxSize clientSize = GetClientSize();
	SetMinSize(GetSizer()->GetMinSize() + size - clientSize);
	SetClientSize(minSize);

	tree_->SelectItem(m_bookmarks_global);

	return ShowModal();
}

void CBookmarksDialog::SaveGlobalBookmarks()
{
	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxString msg = file.GetError() + _T("\n\n") + _("The global bookmarks could not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	{
		auto bookmark = element.child("Bookmark");
		while (bookmark) {
			element.remove_child(bookmark);
			bookmark = element.child("Bookmark");
		}
	}

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = tree_->GetFirstChild(m_bookmarks_global, cookie); child.IsOk(); child = tree_->GetNextChild(m_bookmarks_global, cookie)) {
		CBookmarkItemData *data = (CBookmarkItemData *)tree_->GetItemData(child);
		wxASSERT(data);

		auto bookmark = element.append_child("Bookmark");
		AddTextElement(bookmark, "Name", tree_->GetItemText(child).ToStdWstring());
		if (!data->m_local_dir.empty()) {
			AddTextElement(bookmark, "LocalDir", data->m_local_dir);
		}
		if (!data->m_remote_dir.empty()) {
			AddTextElement(bookmark, "RemoteDir", data->m_remote_dir.GetSafePath());
		}
		if (data->m_sync) {
			AddTextElementUtf8(bookmark, "SyncBrowsing", "1");
		}
		if (data->m_comparison) {
			AddTextElementUtf8(bookmark, "DirectoryComparison", "1");
		}
	}

	if (!file.Save()) {
		wxString msg = wxString::Format(_("Could not write \"%s\", the global bookmarks could no be saved: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}

	CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_GLOBALBOOKMARKS);
}

void CBookmarksDialog::SaveSiteSpecificBookmarks()
{
	if (m_site_path.empty()) {
		return;
	}

	if (!CSiteManager::ClearBookmarks(m_site_path)) {
		return;
	}

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = tree_->GetFirstChild(m_bookmarks_site, cookie); child.IsOk(); child = tree_->GetNextChild(m_bookmarks_site, cookie)) {
		CBookmarkItemData *data = (CBookmarkItemData *)tree_->GetItemData(child);
		wxASSERT(data);

		if (!CSiteManager::AddBookmark(m_site_path, tree_->GetItemText(child), data->m_local_dir, data->m_remote_dir, data->m_sync, data->m_comparison)) {
			return;
		}
	}
}

void CBookmarksDialog::OnOK(wxCommandEvent&)
{
	if (!Verify()) {
		return;
	}
	UpdateBookmark();

	SaveGlobalBookmarks();
	SaveSiteSpecificBookmarks();

	EndModal(wxID_OK);
}

void CBookmarksDialog::OnBrowse(wxCommandEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item) {
		return;
	}

	CBookmarkItemData *data = (CBookmarkItemData *)tree_->GetItemData(item);
	if (!data) {
		return;
	}

	wxTextCtrl *pText = XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl);

	wxDirDialog dlg(this, _("Choose the local directory"), pText->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK) {
		pText->ChangeValue(dlg.GetPath());
	}
}

void CBookmarksDialog::OnSelChanging(wxTreeEvent& event)
{
	if (m_is_deleting) {
		return;
	}

	if (!Verify()) {
		event.Veto();
		return;
	}

	UpdateBookmark();
}

void CBookmarksDialog::OnSelChanged(wxTreeEvent&)
{
	DisplayBookmark();
}

bool CBookmarksDialog::Verify()
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item) {
		return true;
	}

	CBookmarkItemData *data = (CBookmarkItemData *)tree_->GetItemData(item);
	if (!data) {
		return true;
	}

	Site const* site;
	if (tree_->GetItemParent(item) == m_bookmarks_site) {
		site = site_;
	}
	else {
		site = 0;
	}

	wxString const remotePathRaw = xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::GetValue);
	if (!remotePathRaw.empty()) {
		CServerPath remotePath;
		if (site) {
			remotePath.SetType(site->GetOriginalServer().GetType());
		}
		if (!remotePath.SetPath(remotePathRaw.ToStdWstring())) {
			xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::SetFocus);
			if (site) {
				wxString msg;
				if (site->GetOriginalServer().GetType() != DEFAULT) {
					msg = wxString::Format(_("Remote path cannot be parsed. Make sure it is a valid absolute path and is supported by the current site's servertype (%s)."), CServer::GetNameFromServerType(site->GetOriginalServer().GetType()));
				}
				else {
					msg = _("Remote path cannot be parsed. Make sure it is a valid absolute path.");
				}
				wxMessageBoxEx(msg);
			}
			else {
				wxMessageBoxEx(_("Remote path cannot be parsed. Make sure it is a valid absolute path."));
			}
			return false;
		}
	}

	wxString const localPath = xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::GetValue);

	if (remotePathRaw.empty() && localPath.empty()) {
		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::SetFocus);
		wxMessageBoxEx(_("You need to enter at least one path, empty bookmarks are not supported."));
		return false;
	}

	bool const sync = xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::GetValue);
	if (sync && (localPath.empty() || remotePathRaw.empty())) {
		wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this bookmark."), _("New bookmark"), wxICON_EXCLAMATION, this);
		return false;
	}

	return true;
}

void CBookmarksDialog::UpdateBookmark()
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item) {
		return;
	}

	CBookmarkItemData *data = (CBookmarkItemData *)tree_->GetItemData(item);
	if (!data) {
		return;
	}

	Site const* site;
	if (tree_->GetItemParent(item) == m_bookmarks_site) {
		site = site_;
	}
	else {
		site = 0;
	}

	wxString const remotePathRaw = xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::GetValue);
	if (!remotePathRaw.empty()) {
		if (site) {
			data->m_remote_dir.SetType(site->GetOriginalServer().GetType());
		}
		data->m_remote_dir.SetPath(remotePathRaw.ToStdWstring());
	}

	data->m_local_dir = xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::GetValue).ToStdWstring();

	data->m_sync = xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::GetValue);
	data->m_comparison = xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::GetValue);
}

void CBookmarksDialog::DisplayBookmark()
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item) {
		xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::ChangeValue, L"");
		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::ChangeValue, L"");
		xrc_call(*this, "ID_DELETE", &wxButton::Enable, false);
		xrc_call(*this, "ID_RENAME", &wxButton::Enable, false);
		xrc_call(*this, "ID_COPY", &wxButton::Enable, false);
		xrc_call(*this, "ID_NOTEBOOK", &wxNotebook::Enable, false);
		return;
	}

	CBookmarkItemData *data = (CBookmarkItemData *)tree_->GetItemData(item);
	if (!data) {
		xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::ChangeValue, L"");
		xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::ChangeValue, L"");
		xrc_call(*this, "ID_DELETE", &wxButton::Enable, false);
		xrc_call(*this, "ID_RENAME", &wxButton::Enable, false);
		xrc_call(*this, "ID_COPY", &wxButton::Enable, false);
		xrc_call(*this, "ID_NOTEBOOK", &wxNotebook::Enable, false);
		return;
	}

	xrc_call(*this, "ID_DELETE", &wxButton::Enable, true);
	xrc_call(*this, "ID_RENAME", &wxButton::Enable, true);
	xrc_call(*this, "ID_COPY", &wxButton::Enable, true);
	xrc_call(*this, "ID_NOTEBOOK", &wxNotebook::Enable, true);

	xrc_call(*this, "ID_BOOKMARK_REMOTEDIR", &wxTextCtrl::ChangeValue, data->m_remote_dir.GetPath());
	xrc_call(*this, "ID_BOOKMARK_LOCALDIR", &wxTextCtrl::ChangeValue, data->m_local_dir);

	xrc_call(*this, "ID_BOOKMARK_SYNC", &wxCheckBox::SetValue, data->m_sync);
	xrc_call(*this, "ID_BOOKMARK_COMPARISON", &wxCheckBox::SetValue, data->m_comparison);
}

void CBookmarksDialog::OnNewBookmark(wxCommandEvent&)
{
	if (!Verify()) {
		return;
	}
	UpdateBookmark();

	wxTreeItemId item = tree_->GetSelection();
	if (!item) {
		item = m_bookmarks_global;
	}

	if (tree_->GetItemData(item)) {
		item = tree_->GetItemParent(item);
	}

	if (item == m_bookmarks_site) {

		std::unique_ptr<Site> site;
		if (!m_site_path.empty()) {
			site = CSiteManager::GetSiteByPath(m_site_path).first;
		}

		if (!site) {
			if (wxMessageBoxEx(_("Site-specific bookmarks require the server to be stored in the Site Manager.\nAdd current connection to the site manager?"), _("New bookmark"), wxYES_NO | wxICON_QUESTION, this) != wxYES) {
				return;
			}

			m_site_path = CSiteManager::AddServer(*site_);
			if (m_site_path.empty()) {
				wxMessageBoxEx(_("Could not add connection to Site Manager"), _("New bookmark"), wxICON_EXCLAMATION, this);
				return;
			}
		}
	}

	wxString newName = _("New bookmark");
	int index = 2;
	for (;;) {
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = tree_->GetFirstChild(item, cookie);
		bool found = false;
		while (child.IsOk()) {
			wxString name = tree_->GetItemText(child);
			int cmp = name.CmpNoCase(newName);
			if (!cmp) {
				found = true;
				break;
			}

			child = tree_->GetNextChild(item, cookie);
		}
		if (!found) {
			break;
		}

		newName = _("New bookmark") + wxString::Format(_T(" %d"), index++);
	}

	wxTreeItemId child = tree_->AppendItem(item, newName, 1, 1, new CBookmarkItemData);
	tree_->SortChildren(item);
	tree_->SelectItem(child);
	tree_->EditLabel(child);
}

void CBookmarksDialog::OnRename(wxCommandEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item || item == m_bookmarks_global || item == m_bookmarks_site) {
		return;
	}

	tree_->EditLabel(item);
}

void CBookmarksDialog::OnDelete(wxCommandEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item || item == m_bookmarks_global || item == m_bookmarks_site) {
		return;
	}

	wxTreeItemId parent = tree_->GetItemParent(item);

	m_is_deleting = true;
	tree_->Delete(item);
	tree_->SelectItem(parent);
	m_is_deleting = false;
}

void CBookmarksDialog::OnCopy(wxCommandEvent&)
{
	wxTreeItemId item = tree_->GetSelection();
	if (!item.IsOk()) {
		return;
	}

	if (!Verify()) {
		return;
	}

	CBookmarkItemData* data = static_cast<CBookmarkItemData *>(tree_->GetItemData(item));
	if (!data) {
		return;
	}

	UpdateBookmark();

	wxTreeItemId parent = tree_->GetItemParent(item);

	const wxString oldName = tree_->GetItemText(item);
	wxString newName = wxString::Format(_("Copy of %s"), oldName);
	int index = 2;
	for (;;) {
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = tree_->GetFirstChild(parent, cookie);
		bool found = false;
		while (child.IsOk()) {
			wxString name = tree_->GetItemText(child);
			int cmp = name.CmpNoCase(newName);
			if (!cmp) {
				found = true;
				break;
			}

			child = tree_->GetNextChild(parent, cookie);
		}
		if (!found) {
			break;
		}

		newName = wxString::Format(_("Copy (%d) of %s"), index++, oldName);
	}

	CBookmarkItemData* newData = new CBookmarkItemData(*data);
	wxTreeItemId newItem = tree_->AppendItem(parent, newName, 1, 1, newData);

	tree_->SortChildren(parent);
	tree_->SelectItem(newItem);
	tree_->EditLabel(newItem);
}

void CBookmarksDialog::OnBeginLabelEdit(wxTreeEvent& event)
{
	wxTreeItemId item = event.GetItem();
	if (item != tree_->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	if (!item || item == m_bookmarks_global || item == m_bookmarks_site) {
		event.Veto();
		return;
	}
}

void CBookmarksDialog::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled()) {
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (item != tree_->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	if (!item || item == m_bookmarks_global || item == m_bookmarks_site) {
		event.Veto();
		return;
	}

	wxString name = event.GetLabel();
	name = name.substr(0, 255);

	wxTreeItemId parent = tree_->GetItemParent(item);

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = tree_->GetFirstChild(parent, cookie); child.IsOk(); child = tree_->GetNextChild(parent, cookie)) {
		if (child == item) {
			continue;
		}
		if (!name.CmpNoCase(tree_->GetItemText(child))) {
			wxMessageBoxEx(_("Name already exists"), _("Cannot rename entry"), wxICON_EXCLAMATION, this);
			event.Veto();
			return;
		}
	}

	// Always veto and manually change name so that the sorting works
	event.Veto();
	tree_->SetItemText(item, name);
	tree_->SortChildren(parent);
}

bool CBookmarksDialog::GetGlobalBookmarks(std::vector<std::wstring> &bookmarks)
{
	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	for (auto bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		std::wstring name;
		std::wstring local_dir;
		std::wstring remote_dir_raw;
		CServerPath remote_dir;

		name = GetTextElement(bookmark, "Name");
		if (name.empty()) {
			continue;
		}

		local_dir = GetTextElement(bookmark, "LocalDir");
		remote_dir_raw = GetTextElement(bookmark, "RemoteDir");
		if (!remote_dir_raw.empty()) {
			if (!remote_dir.SetSafePath(remote_dir_raw)) {
				continue;
			}
		}
		if (local_dir.empty() && remote_dir.empty()) {
			continue;
		}

		bookmarks.push_back(name);
	}

	return true;
}

bool CBookmarksDialog::GetBookmark(const wxString &name, wxString &local_dir, CServerPath &remote_dir, bool &sync, bool &comparison)
{
	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	for (auto bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		std::wstring remote_dir_raw;

		if (name != GetTextElement(bookmark, "Name")) {
			continue;
		}

		local_dir = GetTextElement(bookmark, "LocalDir");
		remote_dir_raw = GetTextElement(bookmark, "RemoteDir");
		if (!remote_dir_raw.empty()) {
			if (!remote_dir.SetSafePath(remote_dir_raw)) {
				return false;
			}
		}
		if (local_dir.empty() && remote_dir_raw.empty()) {
			return false;
		}

		if (local_dir.empty() || remote_dir_raw.empty()) {
			sync = false;
		}
		else {
			sync = GetTextElementBool(bookmark, "SyncBrowsing", false);
		}

		comparison = GetTextElementBool(bookmark, "DirectoryComparison", false);
		return true;
	}

	return false;
}


bool CBookmarksDialog::AddBookmark(const wxString &name, const wxString &local_dir, const CServerPath &remote_dir, bool sync, bool comparison)
{
	if (local_dir.empty() && remote_dir.empty()) {
		return false;
	}
	if ((local_dir.empty() || remote_dir.empty()) && sync) {
		return false;
	}

	CInterProcessMutex mutex(MUTEX_GLOBALBOOKMARKS);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("bookmarks")));
	auto element = file.Load();
	if (!element) {
		wxString msg = file.GetError() + _T("\n\n") + _("The bookmark could not be added.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	pugi::xml_node bookmark, insertBefore;
	for (bookmark = element.child("Bookmark"); bookmark; bookmark = bookmark.next_sibling("Bookmark")) {
		wxString remote_dir_raw;

		wxString old_name = GetTextElement(bookmark, "Name");

		if (!name.CmpNoCase(old_name)) {
			wxMessageBoxEx(_("Name of bookmark already exists."), _("New bookmark"), wxICON_EXCLAMATION);
			return false;
		}
		if (name < old_name && !insertBefore) {
			insertBefore = bookmark;
		}
	}

	if (insertBefore) {
		bookmark = element.insert_child_before("Bookmark", insertBefore);
	}
	else {
		bookmark = element.append_child("Bookmark");
	}
	AddTextElement(bookmark, "Name", name.ToStdWstring());
	if (!local_dir.empty()) {
		AddTextElement(bookmark, "LocalDir", local_dir.ToStdWstring());
	}
	if (!remote_dir.empty()) {
		AddTextElement(bookmark, "RemoteDir", remote_dir.GetSafePath());
	}
	if (sync) {
		AddTextElementUtf8(bookmark, "SyncBrowsing", "1");
	}
	if (comparison) {
		AddTextElementUtf8(bookmark, "DirectoryComparison", "1");
	}

	if (!file.Save()) {
		wxString msg = wxString::Format(_("Could not write \"%s\", the bookmark could not be added: %s"), file.GetFileName(), file.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
		return false;
	}

	CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_GLOBALBOOKMARKS);

	return true;
}
