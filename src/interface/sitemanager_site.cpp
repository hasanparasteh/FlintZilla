#include "filezilla.h"
#include "sitemanager_site.h"

#include "filezillaapp.h"
#include "Options.h"
#include "sitemanager_controls.h"
#include "sitemanager_dialog.h"
#include "textctrlex.h"
#include "xrc_helper.h"

#include "../include/s3sse.h"

#include <libfilezilla/translate.hpp>

#include <wx/dcclient.h>
#include <wx/gbsizer.h>
#include <wx/statline.h>
#include <wx/wupdlock.h>

#ifdef __WXMSW__
#include "commctrl.h"
#endif

CSiteManagerSite::CSiteManagerSite(CSiteManagerDialog &sitemanager)
    : sitemanager_(sitemanager)
{
}

bool CSiteManagerSite::Load(wxWindow* parent)
{
	Create(parent, -1);

	DialogLayout lay(static_cast<wxTopLevelWindow*>(wxGetTopLevelParent(parent)));

	{
		auto onChange = [this](ServerProtocol protocol, LogonType type) {
			wxWindowUpdateLocker l(this);
			SetControlVisibility(protocol, type);
			for (auto & controls : controls_) {
				controls->SetControlState();
			}
			GetContainingSizer()->Layout();
		};

		generalPage_ = new wxPanel(this);
		AddPage(generalPage_, _("General"));

		auto* main = lay.createMain(generalPage_, 1);
		controls_.emplace_back(std::make_unique<GeneralSiteControls>(*generalPage_, lay, *main, onChange));

		main->Add(new wxStaticLine(generalPage_), lay.grow);

		auto row = lay.createFlex(0, 1);
		main->Add(row);
		row->Add(new wxStaticText(generalPage_, -1, _("&Background color:")), lay.valign);
		auto colors = new wxChoice(generalPage_, XRCID("ID_COLOR"));
		row->Add(colors, lay.valign);

		main->Add(new wxStaticText(generalPage_, -1, _("Co&mments:")));
		main->Add(new wxTextCtrlEx(generalPage_, XRCID("ID_COMMENTS"), L"", wxDefaultPosition, wxSize(-1, lay.dlgUnits(43)), wxTE_MULTILINE), 1, wxGROW);
		main->AddGrowableRow(main->GetEffectiveRowsCount() - 1);

		for (int i = 0; ; ++i) {
			wxString name = CSiteManager::GetColourName(i);
			if (name.empty()) {
				break;
			}
			colors->AppendString(wxGetTranslation(name));
		}
	}

	{
		advancedPage_ = new wxPanel(this);
		AddPage(advancedPage_, _("Advanced"));
		auto * main = lay.createMain(advancedPage_, 1);
		controls_.emplace_back(std::make_unique<AdvancedSiteControls>(*advancedPage_, lay, *main));
	}

	{
		transferPage_ = new wxPanel(this);
		AddPage(transferPage_, _("Transfer Settings"));
		auto * main = lay.createMain(transferPage_, 1);
		controls_.emplace_back(std::make_unique<TransferSettingsSiteControls>(*transferPage_, lay, *main));
	}

	{
		charsetPage_ = new wxPanel(this);
		AddPage(charsetPage_, _("Charset"));
		auto * main = lay.createMain(charsetPage_, 1);
		controls_.emplace_back(std::make_unique<CharsetSiteControls>(*charsetPage_, lay, *main));

		int const charsetPageIndex = FindPage(charsetPage_);
		m_charsetPageText = GetPageText(charsetPageIndex);
		wxGetApp().GetWrapEngine()->WrapRecursive(charsetPage_, 1.3);
	}

	{
		s3Page_ = new wxPanel(this);
		AddPage(s3Page_, L"S3");
		auto * main = lay.createMain(s3Page_, 1);
		controls_.emplace_back(std::make_unique<S3SiteControls>(*s3Page_, lay, *main));
	}

	{
		dropboxPage_ = new wxPanel(this);
		AddPage(dropboxPage_, L"Dropbox");
		auto * main = lay.createMain(dropboxPage_, 1);
		controls_.emplace_back(std::make_unique<DropboxSiteControls>(*dropboxPage_, lay, *main));
	}

	{
		swiftPage_ = new wxPanel(this);
		AddPage(swiftPage_, L"Swift");
		auto * main = lay.createMain(swiftPage_, 1);
		controls_.emplace_back(std::make_unique<SwiftSiteControls>(*swiftPage_, lay, *main));
	}

	m_totalPages = GetPageCount();

	GetPage(0)->GetSizer()->Fit(GetPage(0));

#ifdef __WXMSW__
	// Make pages at least wide enough to fit all tabs
	HWND hWnd = (HWND)GetHandle();

	int width = 4;
	for (unsigned int i = 0; i < GetPageCount(); ++i) {
		RECT tab_rect{};
		if (TabCtrl_GetItemRect(hWnd, i, &tab_rect)) {
			width += tab_rect.right - tab_rect.left;
		}
	}
#else
	// Make pages at least wide enough to fit all tabs
	int width = 10; // Guessed
	wxClientDC dc(this);
	for (unsigned int i = 0; i < GetPageCount(); ++i) {
		wxCoord w, h;
		dc.GetTextExtent(GetPageText(i), &w, &h);

		width += w;
#ifdef __WXMAC__
		width += 20; // Guessed
#else
		width += 20;
#endif
	}
#endif

	wxSize const descSize = XRCCTRL(*this, "ID_ENCRYPTION_DESC", wxWindow)->GetSize();
	wxSize const encSize = XRCCTRL(*this, "ID_ENCRYPTION", wxWindow)->GetSize();

	int dataWidth = std::max(encSize.GetWidth(), XRCCTRL(*this, "ID_PROTOCOL", wxWindow)->GetSize().GetWidth());

	auto generalSizer = static_cast<wxGridBagSizer*>(xrc_call(*this, "ID_PROTOCOL", &wxWindow::GetContainingSizer));
	width = std::max(width, static_cast<int>(descSize.GetWidth() * 2 + dataWidth + generalSizer->GetHGap() * 3));

	wxSize page_min_size = GetPage(0)->GetSizer()->GetMinSize();
	if (page_min_size.x < width) {
		page_min_size.x = width;
		GetPage(0)->GetSizer()->SetMinSize(page_min_size);
	}

	// Set min height of general page sizer
	generalSizer->SetMinSize(generalSizer->GetMinSize());

	// Set min height of encryption row
	auto encSizer = xrc_call(*this, "ID_ENCRYPTION", &wxWindow::GetContainingSizer);
	encSizer->GetItem(encSizer->GetItemCount() - 1)->SetMinSize(0, std::max(descSize.GetHeight(), encSize.GetHeight()));

	return true;
}

void CSiteManagerSite::SetControlVisibility(ServerProtocol protocol, LogonType type)
{
	for (auto & controls : controls_) {
		controls->SetPredefined(predefined_);
		controls->SetControlVisibility(protocol, type);
	}

	if (charsetPage_) {
		if (CServer::ProtocolHasFeature(protocol, ProtocolFeature::Charset)) {
			if (FindPage(charsetPage_) == wxNOT_FOUND) {
				AddPage(charsetPage_, m_charsetPageText);
			}
		}
		else {
			int const charsetPageIndex = FindPage(charsetPage_);
			if (charsetPageIndex != wxNOT_FOUND) {
				RemovePage(charsetPageIndex);
			}
		}
	}

	if (s3Page_) {
		if (protocol == S3) {
			if (FindPage(s3Page_) == wxNOT_FOUND) {
				AddPage(s3Page_, L"S3");
			}
		}
		else {
			int const s3PageIndex = FindPage(s3Page_);
			if (s3PageIndex != wxNOT_FOUND) {
				RemovePage(s3PageIndex);
			}
		}
	}

	if (swiftPage_) {
		if (protocol == SWIFT) {
			if (FindPage(swiftPage_) == wxNOT_FOUND) {
				AddPage(swiftPage_, L"Swift");
			}
		}
		else {
			int const swiftPageIndex = FindPage(swiftPage_);
			if (swiftPageIndex != wxNOT_FOUND) {
				RemovePage(swiftPageIndex);
			}
		}
	}

	if (dropboxPage_) {
		if (protocol == DROPBOX) {
			if (FindPage(dropboxPage_) == wxNOT_FOUND) {
				AddPage(dropboxPage_, L"Dropbox");
			}
		}
		else {
			int const dropboxPageIndex = FindPage(dropboxPage_);
			if (dropboxPageIndex != wxNOT_FOUND) {
				RemovePage(dropboxPageIndex);
			}
		}
	}

	GetPage(0)->GetSizer()->Fit(GetPage(0));
}

bool CSiteManagerSite::UpdateSite(Site &site, bool silent)
{
	for (auto & controls : controls_) {
		if (!controls->UpdateSite(site, silent)) {
			return false;
		}
	}

	site.comments_ = xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::GetValue).ToStdWstring();
	site.m_colour = CSiteManager::GetColourFromIndex(xrc_call(*this, "ID_COLOR", &wxChoice::GetSelection));

	return true;
}

void CSiteManagerSite::SetSite(Site const& site, bool predefined)
{
	predefined_ = predefined;

	if (site) {
		SetControlVisibility(site.server.GetProtocol(), site.credentials.logonType_);
	}
	else {
		bool const kiosk_mode = COptions::Get()->get_int(OPTION_DEFAULT_KIOSKMODE) != 0;
		auto const logonType = kiosk_mode ? LogonType::ask : LogonType::normal;
		SetControlVisibility(FTP, logonType);
	}

	xrc_call(*this, "ID_COLOR", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_COMMENTS", &wxWindow::Enable, !predefined);

	for (auto & controls : controls_) {
		controls->SetSite(site);
		controls->SetControlState();
	}

	if (!site) {
		xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_COLOR", &wxChoice::Select, 0);
	}
	else {
		xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::ChangeValue, site.comments_);
		xrc_call(*this, "ID_COLOR", &wxChoice::Select, CSiteManager::GetColourIndex(site_colour_to_wx(site.m_colour)));
	}
}
