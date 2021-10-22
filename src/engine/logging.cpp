#include "filezilla.h"

#include "logging_private.h"

#include "../include/engine_options.h"

#include <libfilezilla/util.hpp>

#include <errno.h>

#ifndef FZ_WINDOWS
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

bool CLogging::m_logfile_initialized = false;
#ifdef FZ_WINDOWS
HANDLE CLogging::m_log_fd = INVALID_HANDLE_VALUE;
#else
int CLogging::m_log_fd = -1;
#endif
std::string CLogging::m_prefixes[sizeof(logmsg::type) * 8];
unsigned int CLogging::m_pid;
int CLogging::m_max_size;
fz::native_string CLogging::m_file;

int CLogging::m_refcount = 0;
fz::mutex CLogging::mutex_(false);


class CLoggingOptionsChanged final : public fz::event_handler
{
public:
	CLoggingOptionsChanged(CLogging& logger, COptionsBase& options, fz::event_loop& loop)
		: fz::event_handler(loop)
		, logger_(logger)
		, options_(options)
	{
		logger_.UpdateLogLevel(options_);
		options_.watch(OPTION_LOGGING_DEBUGLEVEL, this);
		options_.watch(OPTION_LOGGING_RAWLISTING, this);
	}

	virtual ~CLoggingOptionsChanged()
	{
		options_.unwatch_all(this);
		remove_handler();
	}

	virtual void operator()(const fz::event_base&)
	{
		 // In worker thread
		logger_.UpdateLogLevel(options_);
	}

	CLogging & logger_;
	COptionsBase& options_;
};

CLogging::CLogging(CFileZillaEnginePrivate & engine)
	: engine_(engine)
{
	{
		fz::scoped_lock l(mutex_);
		++m_refcount;
	}
	UpdateLogLevel(engine.GetOptions());
	optionChangeHandler_ = std::make_unique<CLoggingOptionsChanged>(*this, engine_.GetOptions(), engine.event_loop_);
}

CLogging::~CLogging()
{
	fz::scoped_lock l(mutex_);
	--m_refcount;

	if (!m_refcount) {
#ifdef FZ_WINDOWS
		if (m_log_fd != INVALID_HANDLE_VALUE) {
			CloseHandle(m_log_fd);
			m_log_fd = INVALID_HANDLE_VALUE;
		}
#else
		if (m_log_fd != -1) {
			close(m_log_fd);
			m_log_fd = -1;
		}
#endif
		m_logfile_initialized = false;
	}
}

bool CLogging::InitLogFile(fz::scoped_lock& l)
{
	if (m_logfile_initialized) {
		return true;
	}

	m_logfile_initialized = true;

	m_file = fz::to_native(engine_.GetOptions().get_string(OPTION_LOGGING_FILE));
	if (m_file.empty()) {
		return false;
	}

#ifdef FZ_WINDOWS
	m_log_fd = CreateFile(m_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (m_log_fd == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
#else
	m_log_fd = open(m_file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
	if (m_log_fd == -1) {
		int err = errno;
#endif
		l.unlock(); //Avoid recursion
		log(logmsg::error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
		return false;
	}

	m_prefixes[fz::bitscan_reverse(logmsg::status)] = fz::to_utf8(_("Status:"));
	m_prefixes[fz::bitscan_reverse(logmsg::error)] = fz::to_utf8(_("Error:"));
	m_prefixes[fz::bitscan_reverse(logmsg::command)] = fz::to_utf8(_("Command:"));
	m_prefixes[fz::bitscan_reverse(logmsg::reply)] = fz::to_utf8(_("Response:"));
	m_prefixes[fz::bitscan_reverse(logmsg::debug_warning)] = fz::to_utf8(_("Trace:"));
	m_prefixes[fz::bitscan_reverse(logmsg::debug_info)] = m_prefixes[fz::bitscan_reverse(logmsg::debug_warning)];
	m_prefixes[fz::bitscan_reverse(logmsg::debug_verbose)] = m_prefixes[fz::bitscan_reverse(logmsg::debug_warning)];
	m_prefixes[fz::bitscan_reverse(logmsg::debug_debug)] = m_prefixes[fz::bitscan_reverse(logmsg::debug_warning)];
	m_prefixes[fz::bitscan_reverse(logmsg::listing)] = fz::to_utf8(_("Listing:"));

#if FZ_WINDOWS
	m_pid = static_cast<unsigned int>(GetCurrentProcessId());
#else
	m_pid = static_cast<unsigned int>(getpid());
#endif

	m_max_size = engine_.GetOptions().get_int(OPTION_LOGGING_FILE_SIZELIMIT);
	if (m_max_size < 0) {
		m_max_size = 0;
	}
	else if (m_max_size > 2000) {
		m_max_size = 2000;
	}
	m_max_size *= 1024 * 1024;

	return true;
}

void CLogging::LogToFile(logmsg::type nMessageType, std::wstring const& msg, fz::datetime const& now)
{
	fz::scoped_lock l(mutex_);

	if (!m_logfile_initialized) {
		if (!InitLogFile(l)) {
			return;
		}
	}
#ifdef FZ_WINDOWS
	if (m_log_fd == INVALID_HANDLE_VALUE) {
		return;
	}
#else
	if (m_log_fd == -1) {
		return;
	}
#endif

	std::string const out = fz::sprintf("%s %u %u %s %s"
#ifdef FZ_WINDOWS
		"\r\n",
#else
		"\n",
#endif
		now.format("%Y-%m-%d %H:%M:%S", fz::datetime::local), m_pid, engine_.GetEngineId(), m_prefixes[fz::bitscan_reverse(nMessageType)], fz::to_utf8(msg));

#ifdef FZ_WINDOWS
	if (m_max_size) {
		LARGE_INTEGER size;
		if (!GetFileSizeEx(m_log_fd, &size) || size.QuadPart > m_max_size) {
			CloseHandle(m_log_fd);
			m_log_fd = INVALID_HANDLE_VALUE;

			// m_log_fd might no longer be the original file.
			// Recheck on a new handle. Proteced with a mutex against other processes
			HANDLE hMutex = ::CreateMutexW(nullptr, true, L"FileZilla 3 Logrotate Mutex");
			if (!hMutex) {
				DWORD err = GetLastError();
				l.unlock();
				log(logmsg::error, _("Could not create logging mutex: %s"), GetSystemErrorDescription(err));
				return;
			}

			HANDLE hFile = CreateFileW(m_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile == INVALID_HANDLE_VALUE) {
				DWORD err = GetLastError();

				// Oh dear..
				ReleaseMutex(hMutex);
				CloseHandle(hMutex);

				l.unlock(); // Avoid recursion
				log(logmsg::error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
				return;
			}

			DWORD err{};
			if (GetFileSizeEx(hFile, &size) && size.QuadPart > m_max_size) {
				CloseHandle(hFile);

				// MoveFileEx can fail if trying to access a deleted file for which another process still has
				// a handle. Move it far away first.
				// Todo: Handle the case in which logdir and tmpdir are on different volumes.
				// (Why is everthing so needlessly complex on MSW?)

				wchar_t tempDir[MAX_PATH + 1];
				DWORD res = GetTempPath(MAX_PATH, tempDir);
				if (res && res <= MAX_PATH) {
					tempDir[MAX_PATH] = 0;

					wchar_t tempFile[MAX_PATH + 1];
					res = GetTempFileNameW(tempDir, L"fz3", 0, tempFile);
					if (res) {
						tempFile[MAX_PATH] = 0;
						MoveFileExW((m_file + L".1").c_str(), tempFile, MOVEFILE_REPLACE_EXISTING);
						DeleteFileW(tempFile);
					}
				}
				MoveFileExW(m_file.c_str(), (m_file + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
				m_log_fd = CreateFileW(m_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_DELETE | FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (m_log_fd == INVALID_HANDLE_VALUE) {
					// If this function would return bool, I'd return FILE_NOT_FOUND here.
					err = GetLastError();
				}
			}
			else {
				m_log_fd = hFile;
			}

			if (hMutex) {
				ReleaseMutex(hMutex);
				CloseHandle(hMutex);
			}

			if (err) {
				l.unlock(); // Avoid recursion
				log(logmsg::error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
				return;
			}
		}
	}
	DWORD len = static_cast<DWORD>(out.size());
	DWORD written;
	BOOL res = WriteFile(m_log_fd, out.c_str(), len, &written, nullptr);
	if (!res || written != len) {
		DWORD err = GetLastError();
		CloseHandle(m_log_fd);
		m_log_fd = INVALID_HANDLE_VALUE;
		l.unlock(); // Avoid recursion
		log(logmsg::error, _("Could not write to log file: %s"), GetSystemErrorDescription(err));
	}
#else
	if (m_max_size) {
		struct stat buf;
		int rc = fstat(m_log_fd, &buf);
		while (!rc && buf.st_size > m_max_size) {
			struct flock lock = {};
			lock.l_type = F_WRLCK;
			lock.l_whence = SEEK_SET;
			lock.l_start = 0;
			lock.l_len = 1;

			// Retry through signals
			while ((rc = fcntl(m_log_fd, F_SETLKW, &lock)) == -1 && errno == EINTR);

			// Ignore any other failures
			int fd = open(m_file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
			if (fd == -1) {
				int err = errno;

				close(m_log_fd);
				m_log_fd = -1;

				l.unlock(); // Avoid recursion
				log(logmsg::error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
				return;
			}
			struct stat buf2;
			rc = fstat(fd, &buf2);

			// Different files
			if (!rc && buf.st_ino != buf2.st_ino) {
				close(m_log_fd); // Releases the lock
				m_log_fd = fd;
				buf = buf2;
				continue;
			}

			// The file is indeed the log file and we are holding a lock on it.

			// Rename it
			rc = rename(m_file.c_str(), (m_file + ".1").c_str());
			close(m_log_fd);
			close(fd);

			// Get the new file
			m_log_fd = open(m_file.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
			if (m_log_fd == -1) {
				int err = errno;
				l.unlock(); // Avoid recursion
				log(logmsg::error, _("Could not open log file: %s"), GetSystemErrorDescription(err));
				return;
			}

			if (!rc) {
				// Rename didn't fail
				rc = fstat(m_log_fd, &buf);
			}
		}
	}
	size_t written = write(m_log_fd, out.c_str(), out.size());
	if (written != out.size()) {
		int err = errno;
		close(m_log_fd);
		m_log_fd = -1;

		l.unlock(); // Avoid recursion
		log(logmsg::error, _("Could not write to log file: %s"), GetSystemErrorDescription(err));
	}
#endif
}

void CLogging::UpdateLogLevel(COptionsBase & options)
{
	logmsg::type enabled{};
	switch (options.get_int(OPTION_LOGGING_DEBUGLEVEL)) {
	case 1:
		enabled = logmsg::debug_warning;
		break;
	case 2:
		enabled = static_cast<logmsg::type>(logmsg::debug_warning | logmsg::debug_info);
		break;
	case 3:
		enabled = static_cast<logmsg::type>(logmsg::debug_warning | logmsg::debug_info | logmsg::debug_verbose);
		break;
	case 4:
		enabled = static_cast<logmsg::type>(logmsg::debug_warning | logmsg::debug_info | logmsg::debug_verbose | logmsg::debug_debug);
		break;
	default:
		break;
	}
	if (options.get_int(OPTION_LOGGING_RAWLISTING) != 0) {
		enabled = static_cast<logmsg::type>(enabled | logmsg::listing);
	}

	logmsg::type disabled{ (logmsg::debug_warning | logmsg::debug_info | logmsg::debug_verbose | logmsg::debug_debug | logmsg::listing) & ~enabled };

	enable(enabled);
	disable(disabled);
}
