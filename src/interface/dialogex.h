#ifndef FILEZILLA_INTERFACE_DIALOGEX_HEADER
#define FILEZILLA_INTERFACE_DIALOGEX_HEADER

#include "wrapengine.h"

class wxGridBagSizer;
struct DialogLayout final
{
public:
	int gap{};
	int border{};
	int indent{};

	int dlgUnits(int num) const;

	static wxSizerFlags const grow;
	static wxSizerFlags const halign;
	static wxSizerFlags const valign;
	static wxSizerFlags const valigng;
	static wxSizerFlags const ralign;

	wxFlexGridSizer* createMain(wxWindow* parent, int cols, int rows = 0) const;
	wxFlexGridSizer* createFlex(int cols, int rows = 0) const;
	wxGridSizer* createGrid(int cols, int rows = 0) const;
	wxGridBagSizer* createGridBag(int cols, int rows = 0) const;
	wxStdDialogButtonSizer* createButtonSizer(wxWindow* parent, wxSizer * sizer, bool hline) const;

	std::tuple<wxStaticBox*, wxFlexGridSizer*> createStatBox(wxSizer* parent, wxString const& title, int cols, int rows = 0) const;

	DialogLayout(wxTopLevelWindow * parent);

	void gbNewRow(wxGridBagSizer * gb) const;
	wxSizerItem* gbAddRow(wxGridBagSizer * gb, wxWindow* wnd, wxSizerFlags const& flags = wxSizerFlags()) const;
	wxSizerItem* gbAdd(wxGridBagSizer * gb, wxWindow* wnd, wxSizerFlags const& flags = wxSizerFlags()) const;
	wxSizerItem* gbAdd(wxGridBagSizer* gb, wxSizer* sizer, wxSizerFlags const& flags = wxSizerFlags()) const;

protected:
	wxTopLevelWindow * parent_;
};

class wxDialogEx : public wxDialog, public CWrapEngine
{
public:
	bool Create(wxWindow * parent, int id, wxString const& title, wxPoint const& pos = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = wxDEFAULT_DIALOG_STYLE);

	bool Load(wxWindow *pParent, wxString const& name, std::wstring const& file = std::wstring());

	bool SetChildLabel(int id, const wxString& label, unsigned long maxLength = 0);
	bool SetChildLabel(char const* id, const wxString& label, unsigned long maxLength = 0);
	wxString GetChildLabel(int id);

	virtual int ShowModal();

	bool ReplaceControl(wxWindow* old, wxWindow* wnd);

	static bool CanShowPopupDialog(wxTopLevelWindow * parent = 0);

	DialogLayout const& layout();

	void EndDialog(int rc) {
		wxDialog::EndDialog(rc);
	}

protected:
	virtual void InitDialog();

	DECLARE_EVENT_TABLE()
	virtual void OnChar(wxKeyEvent& event);
	void OnMenuEvent(wxCommandEvent& event);

#ifdef __WXMAC__
	virtual bool ProcessEvent(wxEvent& event);

	static std::vector<void*> shown_dialogs_creation_events_;
#endif

	static std::vector<wxDialogEx*> shown_dialogs_;

	std::unique_ptr<DialogLayout> layout_;

	std::vector<wxAcceleratorEntry> acceleratorTable_;
};

wxWindowID const nullID = wxID_HIGHEST;

std::wstring LabelEscape(std::wstring const& label);

#ifdef __WXMAC__
void FixPasswordPaste(std::vector<wxAcceleratorEntry> & entries);
#endif

#endif
