#ifndef FILEZILLA_ENGINE_WRITER_HEADER
#define FILEZILLA_ENGINE_WRITER_HEADER

#include "aio.h"

#include <libfilezilla/event.hpp>
#include <libfilezilla/event_handler.hpp>
#include <libfilezilla/file.hpp>
#include <libfilezilla/thread_pool.hpp>

class writer_base;

struct write_ready_event_type{};
typedef fz::simple_event<write_ready_event_type, writer_base*> write_ready_event;

class writer_base;

class FZC_PUBLIC_SYMBOL writer_factory
{
public:
	explicit writer_factory(std::wstring const& name) 
		: name_(name)
	{}

	virtual ~writer_factory() noexcept = default;

	static constexpr auto npos = static_cast<uint64_t>(-1);

	virtual std::unique_ptr<writer_factory> clone() const = 0;

	virtual std::unique_ptr<writer_base> open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, bool update_transfer_status = true) = 0;

	std::wstring name() const { return name_; }

	virtual uint64_t size() const { return aio_base::nosize; }
	virtual fz::datetime mtime() const { return fz::datetime(); }
	virtual bool set_mtime(fz::datetime const&) { return false; }

protected:
	writer_factory() = default;
	writer_factory(writer_factory const&) = default;
	writer_factory& operator=(writer_factory const&) = default;

private:
	std::wstring name_;
};

class FZC_PUBLIC_SYMBOL writer_factory_holder final
{
public:
	static constexpr auto npos = static_cast<uint64_t>(-1);

	writer_factory_holder() = default;
	writer_factory_holder(std::unique_ptr<writer_factory> && factory);
	writer_factory_holder(std::unique_ptr<writer_factory> const& factory);
	writer_factory_holder(writer_factory const& factory);

	writer_factory_holder(writer_factory_holder const& op);
	writer_factory_holder& operator=(writer_factory_holder const& op);

	writer_factory_holder(writer_factory_holder && op) noexcept;
	writer_factory_holder& operator=(writer_factory_holder && op) noexcept;
	writer_factory_holder& operator=(std::unique_ptr<writer_factory> && factory);

	std::unique_ptr<writer_base> open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, bool update_transfer_status = true)
	{
		return impl_ ? impl_->open(offset, engine, handler, shm, update_transfer_status) : nullptr;
	}

	std::wstring name() const { return impl_ ? impl_->name() : std::wstring(); }
	uint64_t size() const {	return impl_ ? impl_->size() : aio_base::nosize; }
	fz::datetime mtime() const { return impl_ ? impl_->mtime() : fz::datetime(); }
	bool set_mtime(fz::datetime const& t) { return impl_ ? impl_->set_mtime(t) : false; }

	explicit operator bool() const { return impl_.operator bool(); }

private:
	std::unique_ptr<writer_factory> impl_;
};

class FZC_PUBLIC_SYMBOL file_writer_factory final : public writer_factory
{
public:
	file_writer_factory(std::wstring const& file, bool fsync = false);

	virtual std::unique_ptr<writer_base> open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, bool update_transfer_status = true) override;
	virtual std::unique_ptr<writer_factory> clone() const override;

	virtual uint64_t size() const override;
	virtual fz::datetime mtime() const override;

	virtual bool set_mtime(fz::datetime const&) override;

	bool fsync_{};
};


struct get_write_buffer_result {
	bool operator==(aio_result const t) const { return type_ == t; }
	bool operator!=(aio_result const t) const { return type_ != t; }

	aio_result type_{aio_result::error};
	fz::nonowning_buffer buffer_;
};

class FZC_PUBLIC_SYMBOL writer_base : public aio_base
{
public:
	explicit writer_base(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, bool update_transfer_status);

	virtual void close();

	virtual aio_result finalize(fz::nonowning_buffer & last_written);

	virtual uint64_t size() const { return static_cast<uint64_t>(-1); }

	virtual get_write_buffer_result get_write_buffer(fz::nonowning_buffer & last_written);

	virtual aio_result retire(fz::nonowning_buffer & last_written);

	virtual aio_result preallocate(uint64_t /*size*/) { return aio_result::ok; }

	void set_handler(fz::event_handler * handler);

protected:
	virtual aio_result continue_finalize() { return aio_result::ok; }

	bool finalized_{};
	bool update_transfer_status_{};
};

class FZC_PUBLIC_SYMBOL file_writer final : public writer_base
{
public:
	explicit file_writer(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, bool update_transfer_status);
	~file_writer();

	virtual void close() override;

	virtual uint64_t size() const override;

	virtual aio_result preallocate(uint64_t size) override;

protected:
	virtual void signal_capacity(fz::scoped_lock & l) override;
	virtual aio_result continue_finalize() override;

private:
	friend class file_writer_factory;
	aio_result open(uint64_t offset, bool fsync, shm_flag shm);

	void entry();

	fz::file file_;

	fz::async_task thread_;
	fz::condition cond_;
	bool from_beginning_{};
	bool fsync_{};
	bool preallocated_{};
};

namespace fz {
class buffer;
}

class FZC_PUBLIC_SYMBOL memory_writer_factory final : public writer_factory
{
public:
	memory_writer_factory(std::wstring const& name, fz::buffer & result_buffer, size_t sizeLimit = 0);
	
	virtual std::unique_ptr<writer_base> open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, bool update_transfer_status = true) override;
	virtual std::unique_ptr<writer_factory> clone() const override;

	virtual uint64_t size() const override { return npos; }

private:
	friend class memory_writer;
	
	fz::buffer * result_buffer_{};
	size_t sizeLimit_{};
};


class FZC_PUBLIC_SYMBOL memory_writer final : public writer_base
{
public:
	~memory_writer();

	virtual void close() override;

	virtual uint64_t size() const override;

	std::unique_ptr<memory_writer> create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, bool update_transfer_status, fz::buffer & result_buffer, size_t sizeLimit);

	virtual aio_result preallocate(uint64_t size) override;

protected:
	explicit memory_writer(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, bool update_transfer_status, fz::buffer & result_buffer, size_t sizeLimit);
	virtual void signal_capacity(fz::scoped_lock & l) override;

private:
	friend class memory_writer_factory;
	aio_result open(shm_flag shm);

	fz::buffer & result_buffer_;
	size_t sizeLimit_{};
};
#endif
