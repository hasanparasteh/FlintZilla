#ifndef FILEZILLA_INTERFACE_SITEMANAGER_CONTROLS_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_CONTROLS_HEADER

#include "serverdata.h"

struct DialogLayout;
class Site;
enum ServerProtocol;
enum class LogonType;

class SiteControls
{
public:
	SiteControls(wxWindow & parent)
	    : parent_(parent)
	{}

	virtual ~SiteControls() = default;
	virtual void SetSite(Site const& site) = 0;

	void SetPredefined(bool predefined) { predefined_ = predefined; }
	virtual void SetControlVisibility(ServerProtocol /*protocol*/, LogonType /*type*/) {}

	virtual bool UpdateSite(Site & site, bool silent) = 0;
	virtual void SetControlState() {}

	wxWindow & parent_;

	bool predefined_{};
	ServerProtocol protocol_{UNKNOWN};
	LogonType logonType_{};
};

class GeneralSiteControls final : public SiteControls
{
public:
	GeneralSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer, std::function<void(ServerProtocol protocol, LogonType logon_type)> const& changeHandler = nullptr);

	virtual void SetControlVisibility(ServerProtocol protocol, LogonType) override;
	virtual void SetSite(Site const& site) override;
	virtual void SetControlState() override;

	virtual bool UpdateSite(Site & site, bool silent) override;

private:
	void SetProtocol(ServerProtocol protocol);
	ServerProtocol GetProtocol() const;

	void SetLogonType(LogonType t);
	LogonType GetLogonType() const;

	void UpdateHostFromDefaults(ServerProtocol const newProtocol);

	std::map<ServerProtocol, int> mainProtocolListIndex_;
	typedef std::tuple<std::string, wxStaticText*, wxTextCtrl*> Parameter;
	std::vector<Parameter> extraParameters_[ParameterSection::section_count];

	std::function<void(ServerProtocol protocol, LogonType logon_type)> const changeHandler_;
};

class AdvancedSiteControls final : public SiteControls
{
public:
	AdvancedSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site) override;
	virtual void SetControlVisibility(ServerProtocol protocol, LogonType) override;
	virtual bool UpdateSite(Site & site, bool silent) override;
};

class CharsetSiteControls final : public SiteControls
{
public:
	CharsetSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site) override;
	virtual bool UpdateSite(Site & site, bool silent) override;
};

class TransferSettingsSiteControls final : public SiteControls
{
public:
	TransferSettingsSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site) override;
	virtual void SetControlVisibility(ServerProtocol protocol, LogonType) override;
	virtual bool UpdateSite(Site & site, bool silent) override;
};

class S3SiteControls final : public SiteControls
{
public:
	S3SiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site) override;
	virtual bool UpdateSite(Site & site, bool silent) override;

private:
	virtual void SetControlState() override;
};

class DropboxSiteControls final : public SiteControls
{
public:
	DropboxSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site) override;
	virtual bool UpdateSite(Site & site, bool silent) override;
};

class SwiftSiteControls final : public SiteControls
{
public:
	SwiftSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer);

	virtual void SetSite(Site const& site) override;
	virtual bool UpdateSite(Site & site, bool silent) override;

private:
	virtual void SetControlState() override;
};

#endif
