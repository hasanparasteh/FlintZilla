#include "filezilla.h"
#include "clearprivatedata.h"
#include "commandqueue.h"
#include "local_recursive_operation.h"
#include "Mainfrm.h"
#include "Options.h"
#include "queue.h"
#include "quickconnectbar.h"
#include "recentserverlist.h"
#include "remote_recursive_operation.h"
#include "state.h"

#include "../commonui/ipcmutex.h"

#include <libfilezilla/file.hpp>

BEGIN_EVENT_TABLE(CClearPrivateDataDialog, wxDialogEx)
EVT_TIMER(wxID_ANY, CClearPrivateDataDialog::OnTimer)
END_EVENT_TABLE()

CClearPrivateDataDialog::CClearPrivateDataDialog(CMainFrame* pMainFrame)
	: m_pMainFrame(pMainFrame)
{
}

void CClearPrivateDataDialog::Run()
{
	if (!Load(m_pMainFrame, _T("ID_CLEARPRIVATEDATA"))) {
		return;
	}

	if (ShowModal() != wxID_OK) {
		return;
	}

	wxCheckBox *pSitemanagerCheck = XRCCTRL(*this, "ID_CLEARSITEMANAGER", wxCheckBox);
	wxCheckBox *pQueueCheck = XRCCTRL(*this, "ID_CLEARQUEUE", wxCheckBox);
	if (pSitemanagerCheck->GetValue() && pQueueCheck->GetValue()) {
		int res = wxMessageBoxEx(_("Do you really want to delete all Site Manager entries and the transfer queue?"), _("Clear private data"), wxYES | wxNO | wxICON_QUESTION);
		if (res != wxYES) {
			return;
		}
	}
	else if (pQueueCheck->GetValue()) {
		int res = wxMessageBoxEx(_("Do you really want to delete the transfer queue?"), _("Clear private data"), wxYES | wxNO | wxICON_QUESTION);
		if (res != wxYES) {
			return;
		}
	}
	else if (pSitemanagerCheck->GetValue()) {
		int res = wxMessageBoxEx(_("Do you really want to delete all Site Manager entries?"), _("Clear private data"), wxYES | wxNO | wxICON_QUESTION);
		if (res != wxYES) {
			return;
		}
	}

	wxCheckBox *pCheck = XRCCTRL(*this, "ID_CLEARQUICKCONNECT", wxCheckBox);
	if (pCheck && pCheck->GetValue()) {
		CRecentServerList::Clear();
		if (m_pMainFrame->GetQuickconnectBar()) {
			m_pMainFrame->GetQuickconnectBar()->ClearFields();
		}
	}

	pCheck = XRCCTRL(*this, "ID_CLEARRECONNECT", wxCheckBox);
	if (pCheck && pCheck->GetValue()) {
		bool asked = false;

		const std::vector<CState*> *states = CContextManager::Get()->GetAllStates();

		for (std::vector<CState*>::const_iterator iter = states->begin(); iter != states->end(); ++iter) {
			CState* pState = *iter;
			if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
				if (!asked) {
					int res = wxMessageBoxEx(_("Reconnect information cannot be cleared while connected to a server.\nIf you continue, your connection will be disconnected."), _("Clear private data"), wxOK | wxCANCEL);
					if (res != wxOK) {
						return;
					}
					asked = true;
				}

				pState->GetLocalRecursiveOperation()->StopRecursiveOperation();
				pState->GetRemoteRecursiveOperation()->StopRecursiveOperation();
				if (!pState->m_pCommandQueue->Cancel()) {
					m_timer.SetOwner(this);
					m_timer.Start(250, true);
				}
				else {
					pState->Disconnect();
				}
			}
		}

		// Doesn't harm to do it now, but has to be repeated later just to be safe
		ClearReconnect();
	}

	if (pSitemanagerCheck->GetValue()) {
		CInterProcessMutex sitemanagerMutex(MUTEX_SITEMANAGERGLOBAL, false);
		while (sitemanagerMutex.TryLock() == 0) {
			int res = wxMessageBoxEx(_("The Site Manager is opened in another instance of FileZilla 3.\nPlease close it or the data cannot be deleted."), _("Clear private data"), wxOK | wxCANCEL);
			if (res != wxYES) {
				return;
			}
		}
		CInterProcessMutex mutex(MUTEX_SITEMANAGER);
		RemoveXmlFile(L"sitemanager");
	}

	if (pQueueCheck->GetValue()) {
		m_pMainFrame->GetQueue()->SetActive(false);
		m_pMainFrame->GetQueue()->RemoveAll();
	}
}

void CClearPrivateDataDialog::OnTimer(wxTimerEvent&)
{
	const std::vector<CState*> *states = CContextManager::Get()->GetAllStates();

	for (std::vector<CState*>::const_iterator iter = states->begin(); iter != states->end(); ++iter) {
		CState* pState = *iter;

		if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
			if (!pState->m_pCommandQueue->Cancel()) {
				return;
			}

			pState->Disconnect();
		}

		if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
			return;
		}
	}

	m_timer.Stop();
	ClearReconnect();
	Delete();
}

void CClearPrivateDataDialog::Delete()
{
	if (m_timer.IsRunning()) {
		return;
	}

	Destroy();
}

bool CClearPrivateDataDialog::ClearReconnect()
{
	COptions::Get()->Cleanup();
	COptions::Get()->Save();

	const std::vector<CState*> *states = CContextManager::Get()->GetAllStates();
	for (std::vector<CState*>::const_iterator iter = states->begin(); iter != states->end(); ++iter) {
		CState* pState = *iter;
		if (pState) {
			pState->SetLastSite(Site(), CServerPath());
		}
	}

	return true;
}

void CClearPrivateDataDialog::RemoveXmlFile(std::wstring const& name)
{
	std::wstring const path = COptions::Get()->get_string(OPTION_DEFAULT_SETTINGSDIR);
	if (!name.empty() && !path.empty()) {
		fz::remove_file(fz::to_native(path + name + L".xml"));
		fz::remove_file(fz::to_native(path + name + L".xml~"));
	}
}
