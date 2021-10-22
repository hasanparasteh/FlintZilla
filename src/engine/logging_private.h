#ifndef FILEZILLA_ENGINE_LOGGING_PRIVATE_HEADER
#define FILEZILLA_ENGINE_LOGGING_PRIVATE_HEADER

#include "engineprivate.h"
#include <libfilezilla/format.hpp>
#include <libfilezilla/mutex.hpp>
#include <utility>

class CLoggingOptionsChanged;

class CLogging : public fz::logger_interface
{
public:
	explicit CLogging(CFileZillaEnginePrivate & engine);
	virtual ~CLogging();

	CLogging(CLogging const&) = delete;
	CLogging& operator=(CLogging const&) = delete;

	virtual void do_log(logmsg::type t, std::wstring&& msg) override final {
		auto now = fz::datetime::now();
		LogToFile(t, msg, now);
		engine_.AddLogNotification(std::make_unique<CLogmsgNotification>(t, msg, now));
	}
	
	void UpdateLogLevel(COptionsBase & options);

private:
	CFileZillaEnginePrivate & engine_;

	bool InitLogFile(fz::scoped_lock& l);
	void LogToFile(logmsg::type nMessageType, std::wstring const& msg, fz::datetime const& now);

	static bool m_logfile_initialized;
#ifdef FZ_WINDOWS
	static HANDLE m_log_fd;
#else
	static int m_log_fd;
#endif
	static std::string m_prefixes[sizeof(logmsg::type) * 8];
	static unsigned int m_pid;
	static int m_max_size;
	static fz::native_string m_file;

	static int m_refcount;

	static fz::mutex mutex_;

	std::unique_ptr<CLoggingOptionsChanged> optionChangeHandler_;
};

#endif
