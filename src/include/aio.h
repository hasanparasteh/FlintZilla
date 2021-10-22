#ifndef FILEZILLA_ENGINE_AIO_HEADER
#define FILEZILLA_ENGINE_AIO_HEADER

#include "visibility.h"

#include <libfilezilla/nonowning_buffer.hpp>
#include <libfilezilla/mutex.hpp>

#include <array>
#include <tuple>

enum class aio_result
{
	ok,
	wait,
	error
};

namespace fz {
class event_handler;
}
class CFileZillaEnginePrivate;

class FZC_PUBLIC_SYMBOL aio_base
{
public:
	explicit aio_base(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler);
	virtual ~aio_base();

	aio_base(aio_base const&) = delete;
	aio_base& operator=(aio_base const&) = delete;
	aio_base(aio_base &&) = delete;
	aio_base& operator=(aio_base &&) = delete;

	// The buffer size aimed for, actual buffer size may differ
	static constexpr size_t buffer_size_{256*1024};
	static constexpr size_t buffer_count{8};

#if FZ_WINDOWS
	typedef HANDLE shm_handle;
	static shm_handle const shm_handle_default;

	typedef bool shm_flag;
	static shm_flag constexpr shm_flag_none{false};
#else
	// A file descriptor
	typedef int shm_handle;
	static shm_handle constexpr shm_handle_default{-1};

	typedef shm_handle shm_flag;
	static shm_flag constexpr shm_flag_none{shm_handle_default};
#endif

	std::tuple<shm_handle, uint8_t const*, size_t> shared_memory_info() const;

	static constexpr auto nosize = static_cast<uint64_t>(-1);

	std::wstring const& name() const { return name_; }

protected:
	virtual void signal_capacity(fz::scoped_lock &) {};

	mutable fz::mutex mtx_{false};

	bool allocate_memory(bool single, shm_flag shm);

	std::wstring const name_;

	std::array<fz::nonowning_buffer, buffer_count> buffers_;
	size_t ready_pos_{};
	size_t ready_count_{};

	CFileZillaEnginePrivate & engine_;
	fz::event_handler * handler_{};

	bool processing_{};
	bool quit_{};
	bool error_{};
	bool handler_waiting_{};

private:
	shm_handle mapping_{shm_handle_default};
	size_t memory_size_{};
	uint8_t* memory_{};
};

#endif
