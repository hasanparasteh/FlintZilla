#include "filezilla.h"
#include "filezillaapp.h"
#include "import.h"
#include "sitemanager.h"
#include "xmlfunctions.h"
#include "Options.h"
#include "queue.h"
#include "xrc_helper.h"

#include "../commonui/ipcmutex.h"

#include <wx/filedlg.h>

CImportDialog::CImportDialog(wxWindow* parent, CQueueView* pQueueView)
	: m_parent(parent), m_pQueueView(pQueueView)
{
}

void CImportDialog::Run()
{
	wxFileDialog dlg(m_parent, _("Select file to import settings from"), wxString(),
					_T("FileZilla.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	dlg.CenterOnParent();

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	wxFileName fn(dlg.GetPath());
	wxString const path = fn.GetPath();
	wxString const settingsDir(COptions::Get()->get_string(OPTION_DEFAULT_SETTINGSDIR));
	if (path == settingsDir) {
		wxMessageBoxEx(_("You cannot import settings from FileZilla's own settings directory."), _("Error importing"), wxICON_ERROR, m_parent);
		return;
	}

	CXmlFile fz3(dlg.GetPath().ToStdWstring());
	auto fz3Root = fz3.Load();
	if (fz3Root) {
		bool settings = fz3Root.child("Settings") != 0;
		bool queue = fz3Root.child("Queue") != 0;
		bool sites = fz3Root.child("Servers") != 0;
		bool filters = fz3Root.child("Filters") != 0;

		if (settings || queue || sites || filters) {
			if (!Load(m_parent, _T("ID_IMPORT"))) {
				wxBell();
				return;
			}
			if (!queue) {
				xrc_call(*this, "ID_QUEUE", &wxCheckBox::Hide);
			}
			if (!sites) {
				xrc_call(*this, "ID_SITEMANAGER", &wxCheckBox::Hide);
			}
			if (!settings) {
				xrc_call(*this, "ID_SETTINGS", &wxCheckBox::Hide);
			}
			if (!filters) {
				xrc_call(*this, "ID_FILTERS", &wxCheckBox::Hide);
			}
			GetSizer()->Fit(this);

			if (ShowModal() != wxID_OK) {
				return;
			}

			if (fz3.IsFromFutureVersion()) {
				wxString msg = wxString::Format(_("The file '%s' has been created by a more recent version of FileZilla.\nLoading files created by newer versions can result in loss of data.\nDo you want to continue?"), fz3.GetFileName());
				if (wxMessageBoxEx(msg, _("Detected newer version of FileZilla"), wxICON_QUESTION | wxYES_NO) != wxYES) {
					return;
				}
			}

			if (queue && xrc_call(*this, "ID_QUEUE", &wxCheckBox::IsChecked)) {
				m_pQueueView->ImportQueue(fz3Root.child("Queue"), true);
			}

			if (sites && xrc_call(*this, "ID_SITEMANAGER", &wxCheckBox::IsChecked)) {
				CSiteManager::ImportSites(fz3Root.child("Servers"));
			}

			if (settings && xrc_call(*this, "ID_SETTINGS", &wxCheckBox::IsChecked)) {
				auto settings = fz3Root.child("Settings");
				COptions::Get()->Import(settings);
				wxMessageBoxEx(_("The settings have been imported. You have to restart FileZilla for all settings to have effect."), _("Import successful"), wxOK, this);
			}

			if (filters && xrc_call(*this, "ID_FILTERS", &wxCheckBox::IsChecked)) {
				CFilterManager::Import(fz3Root);
			}

			wxMessageBoxEx(_("The selected categories have been imported."), _("Import successful"), wxOK, this);
			return;
		}
	}

	wxMessageBoxEx(_("File does not contain any importable data."), _("Error importing"), wxICON_ERROR, m_parent);
}
