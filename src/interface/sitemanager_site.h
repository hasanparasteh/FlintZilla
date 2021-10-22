#ifndef FILEZILLA_INTERFACE_SITEMANAGER_SITE_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_SITE_HEADER

#include <wx/notebook.h>

class Site;
class CSiteManagerDialog;
class SiteControls;
class GeneralSiteControls; //remove
class CSiteManagerSite : public wxNotebook
{
public:
	CSiteManagerSite(CSiteManagerDialog & sitemanager);

	bool Load(wxWindow * parent);

	bool UpdateSite(Site &site, bool silent);
	void SetSite(Site const& site, bool predefined);

private:
	void SetControlVisibility(ServerProtocol protocol, LogonType type);

	CSiteManagerDialog & sitemanager_;

	wxNotebookPage *generalPage_{};
	wxNotebookPage *advancedPage_{};
	wxNotebookPage *charsetPage_{};
	wxNotebookPage *transferPage_{};
	wxNotebookPage *s3Page_{};
	wxNotebookPage *dropboxPage_{};
	wxNotebookPage *swiftPage_{};

	std::vector<std::unique_ptr<SiteControls>> controls_;
	wxString m_charsetPageText;
	size_t m_totalPages = -1;

	bool predefined_{};
};

#endif
