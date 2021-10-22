#include "../include/writer.h"
#include "engineprivate.h"
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/translate.hpp>

#include <string.h>

writer_factory_holder::writer_factory_holder(writer_factory_holder const& op)
{
	if (op.impl_) {
		impl_ = op.impl_->clone();
	}
}

writer_factory_holder& writer_factory_holder::operator=(writer_factory_holder const& op)
{
	if (this != &op && op.impl_) {
		impl_ = op.impl_->clone();
	}
	return *this;
}

writer_factory_holder::writer_factory_holder(writer_factory_holder && op) noexcept
{
	impl_ = std::move(op.impl_);
	op.impl_.reset();
}

writer_factory_holder& writer_factory_holder::operator=(writer_factory_holder && op) noexcept
{
	if (this != &op) {
		impl_ = std::move(op.impl_);
		op.impl_.reset();
	}

	return *this;
}

writer_factory_holder::writer_factory_holder(std::unique_ptr<writer_factory> && factory)
	: impl_(std::move(factory))
{
}

writer_factory_holder::writer_factory_holder(std::unique_ptr<writer_factory> const& factory)
	: impl_(factory ? factory->clone() : nullptr)
{
}


writer_factory_holder::writer_factory_holder(writer_factory const& factory)
	: impl_(factory.clone())
{
}


writer_factory_holder& writer_factory_holder::operator=(std::unique_ptr<writer_factory> && factory)
{
	if (impl_ != factory) {
		impl_ = std::move(factory);
	}

	return *this;
}

file_writer_factory::file_writer_factory(std::wstring const& file, bool fsync)
	: writer_factory(file)
	, fsync_(fsync)
{
}

std::unique_ptr<writer_factory> file_writer_factory::clone() const
{
	return std::make_unique<file_writer_factory>(*this);
}

uint64_t file_writer_factory::size() const
{
	auto s = fz::local_filesys::get_size(fz::to_native(name()));
	if (s < 0) {
		return npos;
	}
	else {
		return static_cast<uint64_t>(s);
	}
}

fz::datetime file_writer_factory::mtime() const
{
	return fz::local_filesys::get_modification_time(fz::to_native(name()));
}

bool file_writer_factory::set_mtime(fz::datetime const& t)
{
	return fz::local_filesys::set_modification_time(fz::to_native(name()), t);
}

std::unique_ptr<writer_base> file_writer_factory::open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, bool update_transfer_status)
{
	auto ret = std::make_unique<file_writer>(name(), engine, handler, update_transfer_status);

	if (ret->open(offset, fsync_, shm) != aio_result::ok) {
		ret.reset();
	}

	return ret;
}

namespace {
void remove_writer_events(fz::event_handler * handler, writer_base const* writer)
{
	if (!handler) {
		return;
	}
	auto event_filter = [&](fz::event_loop::Events::value_type const& ev) -> bool {
		if (ev.first != handler) {
			return false;
		}
		else if (ev.second->derived_type() == write_ready_event::type()) {
			return std::get<0>(static_cast<write_ready_event const&>(*ev.second).v_) == writer;
		}
		return false;
	};

	handler->event_loop_.filter_events(event_filter);
}

void change_event_handler(fz::event_handler * old, fz::event_handler * new_handler, writer_base const* writer)
{
	if (!old) {
		return;
	}

	auto event_filter = [&](fz::event_loop::Events::value_type & ev) -> bool {
		if (ev.first == old && ev.second->derived_type() == write_ready_event::type() && std::get<0>(static_cast<write_ready_event const&>(*ev.second).v_) == writer) {
			ev.first = new_handler;
		}
		return false;
	};

	old->event_loop_.filter_events(event_filter);
}
}

writer_base::writer_base(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, bool update_transfer_status)
	: aio_base(name, engine, handler)
	, update_transfer_status_(update_transfer_status)
{}

void writer_base::close()
{
	ready_count_ = 0;
	
	remove_writer_events(handler_, this);
}

get_write_buffer_result writer_base::get_write_buffer(fz::nonowning_buffer & last_written)
{
	fz::scoped_lock l(mtx_);
	if (error_) {
		return {aio_result::error, fz::nonowning_buffer()};
	}

	if (processing_ && last_written) {
		buffers_[(ready_pos_ + ready_count_) % buffers_.size()] = last_written;
		bool signal = !ready_count_;
		++ready_count_;
		if (signal) {
			signal_capacity(l);
		}
	}
	last_written.reset();
	if (ready_count_ >= buffers_.size()) {
		handler_waiting_ = true;
		processing_ = false;
		return {aio_result::wait, fz::nonowning_buffer()};
	}
	else {
		processing_ = true;
		auto b = buffers_[(ready_pos_ + ready_count_) % buffers_.size()];
		b.resize(0);
		return {aio_result::ok, b};
	}
}

aio_result writer_base::retire(fz::nonowning_buffer & last_written)
{
	fz::scoped_lock l(mtx_);
	if (error_) {
		return aio_result::error;
	}

	if (!processing_) {
		return last_written.empty() ? aio_result::ok : aio_result::error;
	}
	processing_ = false;
	if (last_written) {
		buffers_[(ready_pos_ + ready_count_) % buffers_.size()] = last_written;
		bool signal = !ready_count_;
		++ready_count_;
		if (signal) {
			signal_capacity(l);
		}
	}
	last_written.reset();
	return aio_result::ok;
}

aio_result writer_base::finalize(fz::nonowning_buffer & last_written)
{
	fz::scoped_lock l(mtx_);
	if (error_) {
		return aio_result::error;
	}
	if (finalized_) {
		return aio_result::ok;
	}
	if (processing_ && last_written) {
		buffers_[(ready_pos_ + ready_count_) % buffers_.size()] = last_written;
		last_written.reset();
		processing_ = false;
		bool signal = !ready_count_;
		++ready_count_;
		if (signal) {
			signal_capacity(l);
		}
	}
	if (ready_count_) {
		handler_waiting_ = true;
		return aio_result::wait;
	}

	auto res = continue_finalize();
	if (res == aio_result::ok) {
		finalized_ = true;
	}
	return res;
}

void writer_base::set_handler(fz::event_handler * handler)
{
	fz::event_handler * h = handler;
	{
		fz::scoped_lock l(mtx_);
		std::swap(h, handler_);
	}
	if (!handler) {
		remove_writer_events(h, this);
	}
	else {
		change_event_handler(h, handler, this);
	}
}




file_writer::file_writer(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, bool update_transfer_status)
	: writer_base(name, engine, handler, update_transfer_status)
{
}

file_writer::~file_writer()
{
	close();
}

void file_writer::close()
{
	{
		fz::scoped_lock l(mtx_);
		quit_ = true;
		cond_.signal(l);
	}

	thread_.join();

	writer_base::close();

	if (file_.opened()) {
		bool remove{};
		if (from_beginning_ && !file_.position() && !finalized_) {
			// Freshly created file to which nothing has been written.
			remove = true;
		}
		else if (preallocated_) {
			// The file might have been preallocated and the writing stopped before being completed,
			// so always truncate the file before closing it regardless of finalize state.
			file_.truncate();
		}
		file_.close();

		if (remove) {
			engine_.GetLogger().log(logmsg::debug_verbose, L"Deleting empty file '%s'", name());
			fz::remove_file(fz::to_native(name()));
		}
	}
}

aio_result file_writer::open(uint64_t offset, bool fsync, shm_flag shm)
{
	fsync_ = fsync;

	if (!allocate_memory(false, shm)) {
		engine_.GetLogger().log(logmsg::error, fztranslate("Could not allocate memory to open '%s' for writing."), name_);
		return aio_result::error;
	}

	std::wstring tmp;
	CLocalPath local_path(name(), &tmp);
	if (local_path.HasParent()) {
		fz::native_string last_created;
		fz::mkdir(fz::to_native(local_path.GetPath()), true, fz::mkdir_permissions::normal, &last_created);
		if (!last_created.empty()) {
			// Send out notification
			auto n = std::make_unique<CLocalDirCreatedNotification>();
			if (n->dir.SetPath(fz::to_wstring(last_created))) {
				engine_.AddNotification(std::move(n));
			}
		}
	}

	if (!file_.open(fz::to_native(name()), fz::file::writing, offset ? fz::file::existing : fz::file::empty)) {
		engine_.GetLogger().log(logmsg::error, fztranslate("Could not open '%s' for writing."), name_);
		return aio_result::error;
	}

	if (offset) {
		auto const ofs = static_cast<int64_t>(offset);
		if (file_.seek(ofs, fz::file::begin) != ofs) {
			engine_.GetLogger().log(logmsg::error, fztranslate("Could not seek to offset %d in '%s'."), ofs, name_);
			return aio_result::error;
		}
		if (!file_.truncate()) {
			engine_.GetLogger().log(logmsg::error, fztranslate("Could not truncate '%s' to offset %d."), name_, ofs);
			return aio_result::error;
		}
	}
	else {
		from_beginning_ = true;
	}

	thread_ = engine_.GetThreadPool().spawn([this]() { entry(); });
	if (!thread_) {
		engine_.GetLogger().log(logmsg::error, fztranslate("Could not spawn worker thread for writing '%s'."), name_);
		return aio_result::error;
	}

	return aio_result::ok;
}

void file_writer::entry()
{
	fz::scoped_lock l(mtx_);
	while (!quit_ && !error_) {
		if (!ready_count_) {
			if (handler_waiting_) {
				handler_waiting_ = false;
				if (handler_) {
					handler_->send_event<write_ready_event>(this);
				}
				break;
			}

			cond_.wait(l);
			continue;
		}

		fz::nonowning_buffer & b = buffers_[ready_pos_];

		while (!b.empty()) {
			l.unlock();
			auto written = file_.write(b.get(), b.size());
			l.lock();
			if (quit_) {
				return;
			}
			if (written > 0) {
				b.consume(static_cast<size_t>(written));
				if (update_transfer_status_) {
					engine_.transfer_status_.SetMadeProgress();
					engine_.transfer_status_.Update(written);
				}
			}
			else {
				engine_.GetLogger().log(logmsg::error, fztranslate("Could not write to '%s'."), name_);
				error_ = true;
				break;
			}
		}

		ready_pos_ = (ready_pos_ + 1) % buffers_.size();
		--ready_count_;

		if (handler_waiting_) {
			handler_waiting_ = false;
			if (handler_) {
				handler_->send_event<write_ready_event>(this);
			}
		}
	}
}

void file_writer::signal_capacity(fz::scoped_lock & l)
{
	cond_.signal(l);
}

uint64_t file_writer::size() const
{
	auto s = file_.size();
	if (s < 0) {
		return nosize;
	}
	else {
		return static_cast<uint64_t>(s);
	}
}

aio_result file_writer::continue_finalize()
{
	if (fsync_) {
		if (!file_.fsync()) {
			engine_.GetLogger().log(logmsg::error, fztranslate("Could not sync '%s' to disk."), name_);
			error_ = true;
			return aio_result::error;
		}
	}
	return aio_result::ok;
}


aio_result file_writer::preallocate(uint64_t size)
{
	if (error_) {
		return aio_result::error;
	}

	engine_.GetLogger().log(logmsg::debug_info, L"Preallocating %d bytes for the file \"%s\"", size, name_);

	fz::scoped_lock l(mtx_);

	auto oldPos = file_.seek(0, fz::file::current);
	if (oldPos < 0) {
		return aio_result::error;
	}

	auto seek_offet = static_cast<int64_t>(oldPos + size);
	if (file_.seek(seek_offet, fz::file::begin) == seek_offet) {
		if (!file_.truncate()) {
			engine_.GetLogger().log(logmsg::debug_warning, L"Could not preallocate the file");
		}
	}
	if (file_.seek(oldPos, fz::file::begin) != oldPos) {
		engine_.GetLogger().log(logmsg::error, fztranslate("Could not seek to offset %d within '%s'."), oldPos, name_);
		error_ = true;
		return aio_result::error;
	}
	preallocated_ = true;

	return aio_result::ok;
}


#include <libfilezilla/buffer.hpp>

memory_writer_factory::memory_writer_factory(std::wstring const& name, fz::buffer & result_buffer, size_t sizeLimit)
	: writer_factory(name)
	, result_buffer_(&result_buffer)
	, sizeLimit_(sizeLimit)
{
}

std::unique_ptr<writer_factory> memory_writer_factory::clone() const
{
	return std::make_unique<memory_writer_factory>(*this);
}

std::unique_ptr<writer_base> memory_writer_factory::open(uint64_t offset, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, bool update_transfer_status)
{
	if (!result_buffer_ || offset) {
		return nullptr;
	}

	std::unique_ptr<memory_writer> ret(new memory_writer(name(), engine, handler, update_transfer_status, *result_buffer_, sizeLimit_));
	if (ret->open(shm) != aio_result::ok) {
		ret.reset();
	}

	return ret;
}

memory_writer::memory_writer(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, bool update_transfer_status, fz::buffer & result_buffer, size_t sizeLimit)
	: writer_base(name, engine, handler, update_transfer_status)
	, result_buffer_(result_buffer)
	, sizeLimit_(sizeLimit)
{}

std::unique_ptr<memory_writer> memory_writer::create(std::wstring const& name, CFileZillaEnginePrivate & engine, fz::event_handler * handler, aio_base::shm_flag shm, bool update_transfer_status, fz::buffer & result_buffer, size_t sizeLimit)
{
	std::unique_ptr<memory_writer> ret(new memory_writer(name, engine, handler, update_transfer_status, result_buffer, sizeLimit));
	if (ret->open(shm) != aio_result::ok) {
		ret.reset();
	}
	return ret;
}

memory_writer::~memory_writer()
{
	close();
}

void memory_writer::close()
{
	if (!finalized_) {
		result_buffer_.clear();
	}
}

aio_result memory_writer::open(shm_flag shm)
{
	result_buffer_.clear();
	if (!allocate_memory(false, shm)) {
		engine_.GetLogger().log(logmsg::error, fztranslate("Could not allocate memory to open '%s' for writing."), name_);
		return aio_result::error;
	}
	
	return aio_result::ok;
}

uint64_t memory_writer::size() const
{
	fz::scoped_lock l(mtx_);
	return result_buffer_.size();
}

void memory_writer::signal_capacity(fz::scoped_lock &)
{
	--ready_count_;
	auto & b = buffers_[ready_pos_];
	if (sizeLimit_ && b.size() > sizeLimit_ - result_buffer_.size()) {
		engine_.GetLogger().log(logmsg::debug_warning, "Attempting to write %u bytes with only %u remaining", b.size(), sizeLimit_ - result_buffer_.size());
		error_ = true;
	}
	else {
		result_buffer_.append(b.get(), b.size());
		if (update_transfer_status_) {
			engine_.transfer_status_.SetMadeProgress();
			engine_.transfer_status_.Update(b.size());
		}
		b.resize(0);
	}
}

aio_result memory_writer::preallocate(uint64_t size)
{
	if (error_) {
		return aio_result::error;
	}
	
	fz::scoped_lock l(mtx_);
	result_buffer_.reserve(size);

	return aio_result::ok;
}
