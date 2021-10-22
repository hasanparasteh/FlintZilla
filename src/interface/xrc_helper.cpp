#include "filezilla.h"
#include "filezillaapp.h"
#include "xrc_helper.h"

#include <libfilezilla/local_filesys.hpp>

#include <wx/xrc/xh_animatctrl.h>
#include <wx/xrc/xh_bmpbt.h>
#include <wx/xrc/xh_bttn.h>
#include <wx/xrc/xh_chckb.h>
#include <wx/xrc/xh_chckl.h>
#include <wx/xrc/xh_choic.h>
#include <wx/xrc/xh_dlg.h>
#include <wx/xrc/xh_gauge.h>
#include <wx/xrc/xh_listb.h>
#include <wx/xrc/xh_listc.h>
#include <wx/xrc/xh_panel.h>
#include <wx/xrc/xh_radbt.h>
#include <wx/xrc/xh_scwin.h>
#include <wx/xrc/xh_sizer.h>
#include <wx/xrc/xh_spin.h>
#include <wx/xrc/xh_stbmp.h>
#include <wx/xrc/xh_stbox.h>
#include <wx/xrc/xh_stlin.h>
#include <wx/xrc/xh_sttxt.h>
#include "xh_text_ex.h"
#include <wx/xrc/xh_hyperlink.h>

#include <unordered_set>

void InitHandlers(wxXmlResource& res)
{
	res.AddHandler(new wxSizerXmlHandler);
	res.AddHandler(new wxStaticTextXmlHandler);
	res.AddHandler(new wxButtonXmlHandler);
	res.AddHandler(new wxRadioButtonXmlHandler);
	res.AddHandler(new wxTextCtrlXmlHandlerEx);
	res.AddHandler(new wxCheckBoxXmlHandler);
	res.AddHandler(new wxStaticBoxXmlHandler);
	res.AddHandler(new wxStdDialogButtonSizerXmlHandler);
	res.AddHandler(new wxDialogXmlHandler);
	res.AddHandler(new wxPanelXmlHandler);
	res.AddHandler(new wxStaticLineXmlHandler);
	res.AddHandler(new wxStaticBitmapXmlHandler);
	res.AddHandler(new wxHyperlinkCtrlXmlHandler);
}

namespace {
void LoadXrcFile(std::wstring const& file)
{
	static std::unordered_set<std::wstring> seen;

	if (seen.insert(file).second) {
		std::wstring dir = wxGetApp().GetResourceDir().GetPath() + L"xrc/";
		wxXmlResource* pResource = wxXmlResource::Get();
		pResource->LoadFile(wxString(dir + file));
	}
}
}

void InitXrc(std::wstring const& file)
{
	static bool initialized = false;
	if (!initialized) {
		initialized = true;

		wxXmlResource* pResource = wxXmlResource::Get();

#ifndef __WXDEBUG__
		pResource->SetFlags(pResource->GetFlags() | wxXRC_NO_RELOADING);
#endif

		InitHandlers(*pResource);
	}

	fz::local_filesys fs;
	std::wstring dir = wxGetApp().GetResourceDir().GetPath() + L"xrc/";
	if (file.empty()) {
		LoadXrcFile(L"dialogs.xrc");
	}
	else {
		LoadXrcFile(file);
	}
}
