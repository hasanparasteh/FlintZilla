#include "../include/aio.h"

#include "engineprivate.h"

#ifndef FZ_WINDOWS
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef FZ_MAC
#include <sys/stat.h>
#endif

namespace {
size_t get_page_size()
{
#if FZ_WINDOWS
	static size_t const page_size = []() { SYSTEM_INFO i{}; GetSystemInfo(&i); return i.dwPageSize; }();
#else
	static size_t const page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
	return page_size;
}
}

#if FZ_WINDOWS
aio_base::shm_handle const aio_base::shm_handle_default{INVALID_HANDLE_VALUE};
#endif

aio_base::aio_base(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler)
	: name_(name)
	, engine_(engine)
	, handler_(handler)
{}

aio_base::~aio_base()
{
#if FZ_WINDOWS
	if (mapping_ != INVALID_HANDLE_VALUE) {
		if (memory_) {
			UnmapViewOfFile(memory_);
		}
		CloseHandle(mapping_);
	}
#else
	if (mapping_ != -1) {
		if (memory_) {
			munmap(memory_, memory_size_);
		}
	}
#endif
	else {
		delete [] memory_;
	}
}

bool aio_base::allocate_memory(bool single, shm_flag shm)
{
	if (memory_) {
		return true;
	}

	size_t const count = single ? 1 : buffer_count;

	// Since different threads/processes operate on different buffers at the same time
	// seperate them with a padding page to prevent false sharing due to automatic prefetching.
	memory_size_ = (buffer_size_ + get_page_size()) * count + get_page_size();
#if FZ_WINDOWS
	if (shm) {
		HANDLE mapping = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(memory_size_), nullptr);
		if (!mapping || mapping == INVALID_HANDLE_VALUE) {
			DWORD err = GetLastError();
			engine_.GetLogger().log(logmsg::debug_warning, "CreateFileMapping failed with error %u", err);
			return false;
		}
		memory_ = static_cast<uint8_t*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, memory_size_));
		if (!memory_) {
			DWORD err = GetLastError();
			engine_.GetLogger().log(logmsg::debug_warning, "MapViewOfFile failed with error %u", err);
			return false;
		}
		mapping_ = mapping;
#else
	if (shm >= 0) {
#if FZ_MAC
		// There's a bug on macOS: ftruncate can only be called _once_ on a shared memory object.
		// The manpages do not cover this bug, only XNU's bsd/kern/posix_shm.c mentions it.
		struct stat s;
		if (fstat(shm, &s) != 0) {
			int err = errno;
			engine_.GetLogger().log(logmsg::debug_warning, "fstat failed with error %d", err);
			return false;
		}

		if (s.st_size < 0 || static_cast<size_t>(s.st_size) < memory_size_)
#endif
		{
			if (ftruncate(shm, memory_size_) != 0) {
				int err = errno;
				engine_.GetLogger().log(logmsg::debug_warning, "ftruncate failed with error %d", err);
				return false;
			}
		}
		memory_ = static_cast<uint8_t*>(mmap(nullptr, memory_size_, PROT_READ|PROT_WRITE, MAP_SHARED, shm, 0));
		if (!memory_) {
			int err = errno;
			engine_.GetLogger().log(logmsg::debug_warning, "mmap failed with error %d", err);
			return false;
		}
		mapping_ = shm;
#endif
	}
	else {
		memory_ = new(std::nothrow) uint8_t[memory_size_];
		if (!memory_) {
			return false;
		}
	}
	for (size_t i = 0; i < count; ++i) {
		buffers_[i] = fz::nonowning_buffer(memory_ + i * (buffer_size_ + get_page_size()) + get_page_size(), buffer_size_);
	}

	return true;
}

std::tuple<aio_base::shm_handle, uint8_t const*, size_t> aio_base::shared_memory_info() const
{
	return std::make_tuple(mapping_, memory_, memory_size_);
}
