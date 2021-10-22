#ifndef FILEZILLA_COMMONUI_REGISTRY_HEADER
#define FILEZILLA_COMMONUI_REGISTRY_HEADER

#include "visibility.h"

#ifdef FZ_WINDOWS

#include <optional>
#include <string>

#include <libfilezilla/glue/windows.hpp>

class FZCUI_PUBLIC_SYMBOL regkey final
{
public:
	regkey() = default;
	~regkey();

	enum regview {
		regview_native,
		regview_32,
		regview_64
	};

	explicit regkey(HKEY const root, std::wstring const& subkey, bool readonly, regview v = regview_native);

	regkey(regkey const&) = delete;
	regkey& operator=(regkey const&) = delete;

	void close();

	bool open(HKEY const root, std::wstring const& subkey, bool readonly, regview v = regview_native);

	bool has_value(std::wstring const& name) const;

	std::wstring value(std::wstring const& name) const;
	uint64_t int_value(std::wstring const& name) const;

	bool set_value(std::wstring const& name, std::wstring const& value);
	bool set_value(std::wstring const& name, uint64_t value);

	explicit operator bool() const {
		return key_.has_value();
	}

private:
	mutable std::optional<HKEY> key_;
};

#else
#error This file is for Windows only
#endif

#endif