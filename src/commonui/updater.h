#ifndef FILEZILLA_UPDATER_HEADER
#define FILEZILLA_UPDATER_HEADER

#ifdef HAVE_CONFIG_H
#undef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef PACKAGE_VERSION
// Disable updatechecks if we have no version information
#ifdef FZ_MANUALUPDATECHECK
#undef FZ_MANUALUPDATECHECK
#endif
#define FZ_MANUALUPDATECHECK 0
#endif

#ifndef FZ_MANUALUPDATECHECK
#define FZ_MANUALUPDATECHECK 1
#endif

#ifndef FZ_AUTOUPDATECHECK
#if FZ_MANUALUPDATECHECK
#define FZ_AUTOUPDATECHECK 1
#else
#define FZ_AUTOUPDATECHECK 0
#endif
#else
#if FZ_AUTOUPDATECHECK && !FZ_MANUALUPDATECHECK
#undef FZ_AUTOUPDATECHECK
#define FZ_AUTOUPDATECHECK 0
#endif
#endif

#if FZ_MANUALUPDATECHECK

#include "visibility.h"

#include "../include/notification.h"
#include "../include/optionsbase.h"

#include <libfilezilla/buffer.hpp>
#include <libfilezilla/uri.hpp>

#include <functional>
#include <list>

enum updaterOptions : unsigned {
	OPTION_DEFAULT_DISABLEUPDATECHECK,

	OPTION_UPDATECHECK,
	OPTION_UPDATECHECK_INTERVAL,
	OPTION_UPDATECHECK_LASTDATE,
	OPTION_UPDATECHECK_LASTVERSION,
	OPTION_UPDATECHECK_NEWVERSION,
	OPTION_UPDATECHECK_CHECKBETA,

	OPTIONS_UPDATER_NUM
};

unsigned int FZCUI_PUBLIC_SYMBOL register_updater_options();

inline optionsIndex mapOption(updaterOptions opt)
{
	static unsigned int const offset = register_updater_options();

	auto ret = optionsIndex::invalid;
	if (opt < OPTIONS_UPDATER_NUM) {
		return static_cast<optionsIndex>(opt + offset);
	}
	return ret;
}

struct build final
{
	std::wstring url_;
	std::wstring version_;
	std::wstring hash_;
	int64_t size_{-1};
};

enum class resource_type : int
{
	update_dialog,
	overlay
};

struct version_information final
{
	bool empty() const {
		return available_.version_.empty() && !eol_;
	}

	build stable_;
	build beta_;
	build nightly_;

	build available_;

	std::wstring changelog_;

	std::map<resource_type, std::wstring> resources_;

	bool eol_{};
};

enum class UpdaterState
{
	idle,
	failed,
	checking,
	newversion, // There is a new version available, user needs to manually download
	newversion_downloading, // There is a new version available, file is being downloaded
	newversion_ready, // There is a new version available, file has been downloaded
	newversion_stale, // Very old version of FileZilla. Either update checking has been disabled or is otherwise not working.
	eol // Too old of an operating system
};

class CUpdateHandler
{
public:
	virtual void UpdaterStateChanged(UpdaterState s, build const& v) = 0;
};

class memory_writer_factory;
class CFileZillaEngine;
class CFileZillaEngineContext;
class FZCUI_PUBLIC_SYMBOL CUpdater final : protected fz::event_handler
{
public:
	CUpdater(CFileZillaEngineContext& engine_context);
	virtual ~CUpdater();

	void AddHandler(CUpdateHandler& handler);
	void RemoveHandler(CUpdateHandler& handler);

	void Run(bool manual);
	void Reset();

	UpdaterState GetState() const;
	build AvailableBuild() const;
	std::wstring GetChangelog() const;
	std::wstring GetResources(resource_type t) const;

	std::wstring DownloadedFile() const;

	int64_t BytesDownloaded() const; // Returns -1 on error

	std::wstring GetLog() const;

	static CUpdater* GetInstance();

	bool UpdatableBuild() const;

	bool Busy() const;

private:
	bool LongTimeSinceLastCheck() const;

	int Download(std::wstring const& url, std::wstring const& local_file = std::wstring());
	int Request(fz::uri const& uri);
	int ContinueDownload();

	void OnRun(bool manual);
	UpdaterState LoadLocalData();
	bool ShouldCheck(UpdaterState & s);

	bool CreateConnectCommand(std::wstring const& url);
	bool CreateTransferCommand(std::wstring const& url, std::wstring const& local_file);

	fz::uri GetUrl();
	void ProcessNotification(std::unique_ptr<CNotification> && notification);
	void ProcessOperation(COperationNotification const& operation);
	bool FilterOutput();
	void ParseData();
	UpdaterState ProcessFinishedDownload();
	UpdaterState ProcessFinishedData(bool can_download);

	bool VerifyChecksum(std::wstring const& file, int64_t size, std::wstring const& checksum);

	std::wstring GetTempFile() const;
	std::wstring GetFilename(std::wstring const& url) const;
	std::wstring GetLocalFile(build const& b, bool allow_existing);

	void SetState(UpdaterState s);

	void OnEngineEvent(CFileZillaEngine* engine);

	void operator()(fz::event_base const& ev);
	void on_timer(fz::timer_id const&);
	void on_run(bool manual);

	// Begin what needs to be mutexed
	mutable fz::mutex mtx_;

	UpdaterState state_;
	std::wstring local_file_;

	version_information version_information_;

	std::list<CUpdateHandler*> handlers_;

	std::wstring log_;
	// End mutexed data

	fz::buffer output_buffer_;

	CFileZillaEngineContext& engine_context_;
	CFileZillaEngine* engine_{};

	bool m_use_internal_rootcert{};

	std::wstring raw_version_information_;

	fz::timer_id update_timer_{};

	std::deque<std::unique_ptr<CCommand>> pending_commands_;

	bool manual_{};
};

#endif //FZ_MANUALUPDATECHECK

#endif
