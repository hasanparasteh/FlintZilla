#ifndef FILEZILLA_ENGINE_IO_HEADER
#define FILEZILLA_ENGINE_IO_HEADER

#include "aio.h"

#include <libfilezilla/event.hpp>
#include <libfilezilla/event_handler.hpp>
#include <libfilezilla/file.hpp>
#include <libfilezilla/thread_pool.hpp>

class reader_base;

struct read_ready_event_type{};
typedef fz::simple_event<read_ready_event_type, reader_base*> read_ready_event;

class FZC_PUBLIC_SYMBOL reader_factory
{
public:
	explicit reader_factory(std::wstring const& name);
	virtual ~reader_factory() noexcept = default;

	virtual std::unique_ptr<reader_factory> clone() const = 0;

	// If shm_flag is valid, the buffers are allocated in shared memory suitable for communication with child processes
	// On Windows pass a bool, otherwise a valid file descriptor obtained by memfd_create or shm_open.
	virtual std::unique_ptr<reader_base> open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, uint64_t max_size = aio_base::nosize) = 0;

	std::wstring const& name() const { return name_; }
	virtual uint64_t size() const { return aio_base::nosize; }
	virtual fz::datetime mtime() const { return fz::datetime(); }

protected:
	reader_factory() = default;
	reader_factory(reader_factory const&) = default;
	reader_factory& operator=(reader_factory const&) = default;

private:
	std::wstring name_;
};

class FZC_PUBLIC_SYMBOL reader_factory_holder final
{
public:
	reader_factory_holder() = default;
	reader_factory_holder(std::unique_ptr<reader_factory> && factory);
	reader_factory_holder(std::unique_ptr<reader_factory> const& factory);
	reader_factory_holder(reader_factory const& factory);

	reader_factory_holder(reader_factory_holder const& op);
	reader_factory_holder& operator=(reader_factory_holder const& op);

	reader_factory_holder(reader_factory_holder && op) noexcept;
	reader_factory_holder& operator=(reader_factory_holder && op) noexcept;
	reader_factory_holder& operator=(std::unique_ptr<reader_factory> && factory);

	std::unique_ptr<reader_base> open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, uint64_t max_size = aio_base::nosize)
	{
		return impl_ ? impl_->open(offset, engine, handler, shm, max_size) : nullptr;
	}

	std::wstring name() const { return impl_ ? impl_->name() : std::wstring(); }
	uint64_t size() const {	return impl_ ? impl_->size() : aio_base::nosize; }
	fz::datetime mtime() const { return impl_ ? impl_->mtime() : fz::datetime(); }

	explicit operator bool() const { return impl_.operator bool(); }

private:
	std::unique_ptr<reader_factory> impl_;
};

class FZC_PUBLIC_SYMBOL file_reader_factory final : public reader_factory
{
public:
	file_reader_factory(std::wstring const& file);
	
	virtual std::unique_ptr<reader_base> open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, uint64_t max_size = aio_base::nosize) override;
	virtual std::unique_ptr<reader_factory> clone() const override;

	virtual uint64_t size() const override;
	virtual fz::datetime mtime() const override;
};

struct read_result {
	bool operator==(aio_result const t) const { return type_ == t; }

	aio_result type_{aio_result::error};

	// If type is ok and buffer is empty, we're at eof
	fz::nonowning_buffer buffer_;
};

class reader_base : public aio_base
{
public:
	explicit reader_base(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler);

	virtual void close();


	aio_result rewind() { return seek(aio_base::nosize, aio_base::nosize); }
	virtual aio_result seek(uint64_t offset, uint64_t max_size = aio_base::nosize) = 0;

	virtual read_result read();

	uint64_t size() const;

	void set_handler(fz::event_handler * handler);

protected:
	uint64_t start_offset_{};
	uint64_t max_size_{aio_base::nosize};
	uint64_t size_{aio_base::nosize};
	bool called_read_{};
};

class file_reader final : public reader_base
{
public:
	explicit file_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler);
	~file_reader();

	virtual void close() override;

	virtual aio_result seek(uint64_t offset, uint64_t max_size = aio_base::nosize) override;

protected:
	virtual void signal_capacity(fz::scoped_lock & l) override;

private:
	friend class file_reader_factory;
	aio_result open(uint64_t offset, uint64_t max_size, shm_flag shm);

	void entry();

	fz::file file_;

	fz::async_task thread_;
	fz::condition cond_;

	uint64_t remaining_{};
};




namespace fz {
class buffer;
}

// Does not own the data
class FZC_PUBLIC_SYMBOL memory_reader_factory final : public reader_factory
{
public:
	memory_reader_factory(std::wstring const& name, fz::buffer & data);
	memory_reader_factory(std::wstring const& name, std::string_view const& data);
	
	virtual std::unique_ptr<reader_base> open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, uint64_t max_size = aio_base::nosize) override;
	virtual std::unique_ptr<reader_factory> clone() const override;

	virtual uint64_t size() const override {
		return data_.size();
	}

private:
	friend class memory_reader;
	
	std::string_view data_;
};

// Does not own the data
class memory_reader final : public reader_base
{
public:
	explicit memory_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string_view const& data);
	
	virtual aio_result seek(uint64_t offset, uint64_t max_size = aio_base::nosize) override;

	static std::unique_ptr<memory_reader> create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string_view const& data, shm_flag shm = shm_flag_none);

	virtual read_result read() override;

protected:
	friend class memory_reader_factory;
	aio_result open(uint64_t offset, uint64_t max_size, shm_flag shm);

	std::string_view start_data_;
	std::string_view data_;
};

#endif
