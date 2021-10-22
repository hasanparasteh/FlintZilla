#ifndef FILEZILLA_UPDATE_DIALOG_HEADER
#define FILEZILLA_UPDATE_DIALOG_HEADER

#include "../commonui/updater.h"

#if FZ_MANUALUPDATECHECK
#include "dialogex.h"

#include <wx/timer.h>

class wxPanel;
class wxWindow;

class wxHyperlinkEvent;

class CUpdateDialog final : public wxDialogEx, protected CUpdateHandler
{
public:
	CUpdateDialog(wxWindow* parent, CUpdater& updater);
	virtual ~CUpdateDialog();

	virtual int ShowModal();

	static bool IsRunning();

protected:
	virtual void UpdaterStateChanged( UpdaterState s, build const& v );

	void Wrap();

	void InitFooter();

	void UpdateProgress();

	wxWindow* parent_{};
	CUpdater& updater_;

	wxPanel* content_{};
	std::vector<wxPanel*> panels_;

	wxTimer timer_;

	DECLARE_EVENT_TABLE()
	void OnInstall(wxCommandEvent& ev);
	void OnTimer(wxTimerEvent& ev);
	void ShowDetails(wxHyperlinkEvent& ev);
	void ShowDetailsDl(wxHyperlinkEvent& ev);
	void Retry(wxHyperlinkEvent& ev);
	void OnDebugLog(wxCommandEvent&);
};

#endif
#endif
