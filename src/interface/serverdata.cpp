
#include "serverdata.h"
#include "Options.h"
#include "loginmanager.h"

#include <libfilezilla/encode.hpp>

namespace {
wxColour const background_colors[] = {
	wxColour(),
	wxColour(255, 0, 0, 32),
	wxColour(0, 255, 0, 32),
	wxColour(0, 0, 255, 32),
	wxColour(255, 255, 0, 32),
	wxColour(0, 255, 255, 32),
	wxColour(255, 0, 255, 32),
	wxColour(255, 128, 0, 32) };
}

wxColor site_colour_to_wx(site_colour c) {
	auto index = static_cast<size_t>(c);
	if (index < sizeof(background_colors) / sizeof(*background_colors)){
		return background_colors[index];
	}
	return background_colors[0];
}


void protect(ProtectedCredentials& creds)
{
	if (creds.logonType_ != LogonType::normal && creds.logonType_ != LogonType::account) {
		creds.SetPass(L"");
		return;
	}

	bool kiosk_mode = COptions::Get()->get_int(OPTION_DEFAULT_KIOSKMODE) != 0;
	if (kiosk_mode) {
		if (creds.logonType_ == LogonType::normal || creds.logonType_ == LogonType::account) {
			creds.SetPass(L"");
			creds.logonType_ = LogonType::ask;
		}
	}
	else {
		auto key = fz::public_key::from_base64(fz::to_utf8(COptions::Get()->get_string(OPTION_MASTERPASSWORDENCRYPTOR)));
		protect(CLoginManager::Get(), creds, key);
	}
}
