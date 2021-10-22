#include "string_reader.h"
#include "engineprivate.h"
#include <libfilezilla/translate.hpp>

#include <string.h>

string_reader::string_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string const& data)
	: reader_base(name, engine, handler)
	, start_data_(data)
	, data_(start_data_)
{
	size_ = start_data_.size();
}

string_reader::string_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string && data)
	: reader_base(name, engine, handler)
	, start_data_(std::move(data))
	, data_(start_data_)
{
	size_ = start_data_.size();
}

std::unique_ptr<string_reader> string_reader::create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string const& data, shm_flag shm)
{
	std::unique_ptr<string_reader> ret(new string_reader(name, engine, handler, data));
	if (!ret->allocate_memory(true, shm)) {
		engine.GetLogger().log(logmsg::error, fztranslate("Could not allocate memory to open '%s' for reading."), name);
		ret.reset();
	}

	return ret;
}

std::unique_ptr<string_reader> string_reader::create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, std::string && data, shm_flag shm)
{
	std::unique_ptr<string_reader> ret(new string_reader(name, engine, handler, data));
	if (!ret->allocate_memory(true, shm)) {
		engine.GetLogger().log(logmsg::error, fztranslate("Could not allocate memory to open '%s' for reading."), name);
		ret.reset();
	}

	return ret;
}


read_result string_reader::read()
{
	if (error_) {
		return {aio_result::error, fz::nonowning_buffer()};
	}

	size_t const c = std::min(data_.size(), buffer_size_);

	auto& b = buffers_[0];
	b.resize(c);
	if (c) {
		memcpy(b.get(), data_.data(), c);
		data_ = data_.substr(c);
	}
	return {aio_result::ok, b};
}


aio_result string_reader::seek(uint64_t offset, uint64_t max_size)
{
	if (offset != aio_base::nosize) {
		start_offset_ = offset;
		max_size_ = max_size;
	}

	if (start_offset_ > start_data_.size()) {
		engine_.GetLogger().log(logmsg::error, fztranslate("Could not seek to offset %d in '%s' of size %d."), start_offset_, name_, start_data_.size());
		error_ = true;
		return aio_result::error;
	}
	size_ = start_data_.size() - start_offset_;
	if (max_size_ != aio_base::nosize) {
		if (max_size_ < size_) {
			size_ = max_size_;
		}
	}

	data_ = start_data_;
	data_ = data_.substr(start_offset_, size_);
	return aio_result::ok;
}




buffer_reader::buffer_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, fz::buffer const& data)
	: reader_base(name, engine, handler)
	, start_data_(data)
	, data_(reinterpret_cast<char const*>(start_data_.get()), start_data_.size())
{
	size_ = start_data_.size();
}

buffer_reader::buffer_reader(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, fz::buffer && data)
	: reader_base(name, engine, handler)
	, start_data_(std::move(data))
	, data_(reinterpret_cast<char const*>(start_data_.get()), start_data_.size())
{
	size_ = start_data_.size();
}

std::unique_ptr<buffer_reader> buffer_reader::create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, fz::buffer const& data, shm_flag shm)
{
	std::unique_ptr<buffer_reader> ret(new buffer_reader(name, engine, handler, data));
	if (!ret->allocate_memory(true, shm)) {
		engine.GetLogger().log(logmsg::error, fztranslate("Could not allocate memory to open '%s' for reading."), name);
		ret.reset();
	}

	return ret;
}

std::unique_ptr<buffer_reader> buffer_reader::create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, fz::buffer && data, shm_flag shm)
{
	std::unique_ptr<buffer_reader> ret(new buffer_reader(name, engine, handler, data));
	if (!ret->allocate_memory(true, shm)) {
		engine.GetLogger().log(logmsg::error, fztranslate("Could not allocate memory to open '%s' for reading."), name);
		ret.reset();
	}

	return ret;
}

read_result buffer_reader::read()
{
	if (error_) {
		return {aio_result::error, fz::nonowning_buffer()};
	}

	size_t const c = std::min(data_.size(), buffer_size_);

	auto& b = buffers_[0];
	b.resize(c);
	if (c) {
		memcpy(b.get(), data_.data(), c);
		data_ = data_.substr(c);
	}
	return {aio_result::ok, b};
}


aio_result buffer_reader::seek(uint64_t offset, uint64_t max_size)
{
	if (offset != aio_base::nosize) {
		start_offset_ = offset;
		max_size_ = max_size;
	}

	if (start_offset_ > start_data_.size()) {
		engine_.GetLogger().log(logmsg::error, fztranslate("Could not seek to offset %d in '%s' of size %d."), start_offset_, name_, start_data_.size());
		error_ = true;
		return aio_result::error;
	}
	size_ = start_data_.size() - start_offset_;
	if (max_size_ != aio_base::nosize) {
		if (max_size_ < size_) {
			size_ = max_size_;
		}
	}

	data_ = std::string_view(reinterpret_cast<char const*>(start_data_.get() + start_offset_), size_);
	return aio_result::ok;
}
