#ifndef FILEZILLA_ENGINE_STRING_READER_HEADER
#define FILEZILLA_ENGINE_STRING_READER_HEADER

#include "../include/reader.h"

#include <libfilezilla/buffer.hpp>

// Owns the data
class string_reader final : public reader_base
{
public:

	virtual aio_result seek(uint64_t offset, uint64_t max_size = aio_base::nosize) override;

	static std::unique_ptr<string_reader> create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string const& data, shm_flag shm = shm_flag_none);
	static std::unique_ptr<string_reader> create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string && data, shm_flag shm = shm_flag_none);

	virtual read_result read() override;

protected:
	explicit string_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string const& data);
	explicit string_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string && data);

protected:
	std::string start_data_;
	std::string_view data_;
};


class buffer_reader final : public reader_base
{
public:

	virtual aio_result seek(uint64_t offset, uint64_t max_size = aio_base::nosize) override;

	static std::unique_ptr<buffer_reader> create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, fz::buffer const& data, shm_flag shm = shm_flag_none);
	static std::unique_ptr<buffer_reader> create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, fz::buffer && data, shm_flag shm = shm_flag_none);

	virtual read_result read() override;

protected:
	explicit buffer_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, fz::buffer const& data);
	explicit buffer_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, fz::buffer && data);


protected:
	fz::buffer start_data_;
	std::string_view data_;
};

#endif
