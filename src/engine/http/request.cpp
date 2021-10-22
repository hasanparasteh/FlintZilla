#include "../filezilla.h"

#include "request.h"

#include <libfilezilla/encode.hpp>

#include <string.h>

CHttpRequestOpData::CHttpRequestOpData(CHttpControlSocket & controlSocket, std::shared_ptr<HttpRequestResponseInterface> const& request)
	: COpData(PrivCommand::http_request, L"CHttpRequestOpData")
	, CHttpOpData(controlSocket)
	, fz::event_handler(engine_.event_loop_)
{
	opState = request_init | request_reading;

	request->request().flags_ &= (HttpRequest::flag_update_transferstatus | HttpRequest::flag_confidential_querystring);
	request->response().flags_ = 0;

	requests_.emplace_back(request);
}

CHttpRequestOpData::CHttpRequestOpData(CHttpControlSocket & controlSocket, std::deque<std::shared_ptr<HttpRequestResponseInterface>> && requests)
	: COpData(PrivCommand::http_request, L"CHttpRequestOpData")
	, CHttpOpData(controlSocket)
	, fz::event_handler(engine_.event_loop_)
	, requests_(requests)
{
	for (auto & rr : requests_) {
		rr->request().flags_ &= (HttpRequest::flag_update_transferstatus | HttpRequest::flag_confidential_querystring);
		rr->response().flags_ = 0;
	}
	opState = request_init | request_reading;
}

CHttpRequestOpData::~CHttpRequestOpData()
{
	for (auto & rr : requests_) {
		if (rr) {
			if (rr->request().body_) {
				rr->request().body_->set_handler(nullptr);
			}
		}
	}
	if (!requests_.empty() && requests_.front() && requests_.front()->response().writer_) {
		requests_.front()->response().writer_->set_handler(nullptr);
	}

	remove_handler();
}

void CHttpRequestOpData::AddRequest(std::shared_ptr<HttpRequestResponseInterface> const& rr)
{
	if (!(opState & request_send_mask)) {
		bool wait = false;
		if (!requests_.empty()) {
			if (!requests_.back() && !read_state_.keep_alive_) {
				wait = true;
			}
			else if (requests_.back() && !(requests_.back()->request().keep_alive() || requests_.back()->response().keep_alive())) {
				wait = true;
			}
		}
		if (wait) {
			opState |= request_send_wait_for_read;
		}
		else {
			opState |= request_init;
			if (controlSocket_.active_layer_) {
				controlSocket_.send_event<fz::socket_event>(controlSocket_.active_layer_, fz::socket_event_flag::write, 0);
			}
		}
	}
	rr->request().flags_ &= (HttpRequest::flag_update_transferstatus | HttpRequest::flag_confidential_querystring);
	rr->response().flags_ = 0;
	requests_.push_back(rr);
}

int CHttpRequestOpData::Send()
{
	if (!controlSocket_.send_buffer_.empty()) {
		return FZ_REPLY_WOULDBLOCK;
	}

	if (opState & request_init) {
		if (send_pos_ >= requests_.size()) {
			opState &= ~request_init;
			return FZ_REPLY_CONTINUE;
		}

		auto & rr = *requests_[send_pos_];
		auto & req = rr.request();

		// Check backoff
		fz::duration backoff = controlSocket_.throttler_.get_throttle(req.uri_.host_);
		if (backoff) {
			if (backoff >= fz::duration::from_seconds(30)) {
				log(logmsg::status, _("Server instructed us to wait %d seconds before sending next request"), backoff.get_seconds());
			}
			controlSocket_.Sleep(backoff);
			return FZ_REPLY_CONTINUE;
		}

		int res = req.reset();
		if (res != FZ_REPLY_CONTINUE) {
			return res;
		}
		if (req.body_) {
			req.body_->set_handler(this);
		}

		res = rr.response().reset();
		if (res != FZ_REPLY_CONTINUE) {
			return res;
		}

		if (req.verb_.empty()) {
			log(logmsg::debug_warning, L"No request verb");
			return FZ_REPLY_INTERNALERROR;
		}
		std::string host_header = req.uri_.host_;
		if (req.uri_.port_ != 0) {
			host_header += ':';
			host_header += fz::to_string(req.uri_.port_);
		}
		req.headers_["Host"] = host_header;
		auto pos = req.headers_.find("Connection");
		if (pos == req.headers_.end()) {
			// TODO: consider making keep-alive the default
			req.headers_["Connection"] = "close";
		}
		req.headers_["User-Agent"] = fz::replaced_substrings(PACKAGE_STRING, " ", "/");

		opState &= ~request_init;
		opState |= request_wait_connect;
		return FZ_REPLY_CONTINUE;
	}

	if (opState & request_send_wait_for_read) {
		if (send_pos_ > 0) {
			return FZ_REPLY_WOULDBLOCK;
		}

		opState &= ~request_send_wait_for_read;
		opState |= request_init;

		return FZ_REPLY_CONTINUE;
	}

	if (opState & request_wait_connect) {
		if (send_pos_ >= requests_.size()) {
			log(logmsg::debug_warning, L"Bad state: opState & request_wait_connect yet send_pos_ >= requests_.size()");
			return FZ_REPLY_INTERNALERROR;
		}

		auto & rr = *requests_[send_pos_];
		auto & req = rr.request();

		auto const& uri = req.uri_;
		int res = controlSocket_.InternalConnect(fz::to_wstring_from_utf8(uri.host_), uri.port_, uri.scheme_ == "https", !send_pos_);
		if (res == FZ_REPLY_OK) {
			opState &= ~request_wait_connect;
			opState |= request_send;
			res = FZ_REPLY_CONTINUE;
		}
		return res;
	}

	if (opState & request_send) {
		if (send_pos_ >= requests_.size()) {
			opState &= ~request_send;
		}
		else if (!requests_[send_pos_]) {
			log(logmsg::debug_warning, L"Null request in request_send state.");
			return FZ_REPLY_INTERNALERROR;
		}
		else {
			auto & req = requests_[send_pos_]->request();
			if (!(req.flags_ & HttpRequest::flag_sent_header)) {
				if (!(req.flags_ & HttpRequest::flag_sending_header)) {
					dataToSend_ = req.update_content_length();

					if (dataToSend_ == aio_base::nosize) {
						log(logmsg::error, _("Malformed request header: %s"), _("Invalid Content-Length"));
						return FZ_REPLY_INTERNALERROR;
					}

					req.flags_ |= HttpRequest::flag_sending_header;

					// Assemble request and headers
					std::string command = fz::sprintf("%s %s HTTP/1.1", req.verb_, req.uri_.get_request());
					if (!(req.flags_ & HttpRequest::flag_confidential_querystring)) {
						log(logmsg::command, "%s", command);
					}
					else {
						log(logmsg::command, "%s %s HTTP/1.1", req.verb_, req.uri_.get_request(false));
					}
					command += "\r\n";

					for (auto const& header : req.headers_) {
						std::string line = fz::sprintf("%s: %s", header.first, header.second);
						if (header.first == "Authorization") {
							log(logmsg::command, "%s: %s", header.first, std::string(header.second.size(), '*'));
						}
						else {
							log(logmsg::command, "%s", line);
						}
						command += line + "\r\n";
					}

					command += "\r\n";

					auto result = controlSocket_.Send(command.c_str(), command.size());
					if (result == FZ_REPLY_WOULDBLOCK && !controlSocket_.send_buffer_) {
						result = FZ_REPLY_CONTINUE;
					}
					if (result != FZ_REPLY_CONTINUE) {
						return result;
					}
				}

				req.flags_ |= HttpRequest::flag_sent_header;
				if (!req.body_) {
					log(logmsg::debug_info, "Finished sending request header. Request has no body");
					opState &= ~request_send;
					++send_pos_;
					if (send_pos_ < requests_.size()) {
						if (!req.keep_alive()) {
							opState |= request_send_wait_for_read;
							log(logmsg::debug_info, L"Request did not ask for keep-alive. Waiting for response to finish before sending next request a new connection.");
						}
						else {
							opState |= request_init;
						}
					}
					return FZ_REPLY_CONTINUE;
				}

				log(logmsg::debug_info, "Finished sending request header.");
				sendLogLevel_ = logmsg::debug_debug;

				// Enable Nagle's algorithm if we have a beefy body
				if (req.body_->size() > 536) { // TCPv4 minimum required MSS
					controlSocket_.socket_->set_flags(fz::socket::flag_nodelay, false);
				}
#if FZ_WINDOWS
				// TCP send buffer autotuning
				if (!buffer_tuning_timer_) {
					buffer_tuning_timer_ = add_timer(fz::duration::from_seconds(1), false);
				}
#endif
			}

			while (dataToSend_) {

				if (req.body_buffer_.empty()) {
					read_result r = req.body_->read();
					if (r.type_ == aio_result::wait) {
						return FZ_REPLY_WOULDBLOCK;
					}
					else if (r.type_ == aio_result::error) {
						return FZ_REPLY_ERROR;
					}
					req.body_buffer_ = r.buffer_;

					if (req.body_buffer_.empty() && dataToSend_) {
						log(logmsg::error, _("Unexpected end-of-file on '%s'"), req.body_->name());
						return FZ_REPLY_ERROR;
					}
					else if (req.body_buffer_.size() > dataToSend_) {
						log(logmsg::error, _("Excess data read from '%s'"), req.body_->name());
						return FZ_REPLY_ERROR;
					}
				}

				int error;
				int written = controlSocket_.active_layer_->write(req.body_buffer_.get(), req.body_buffer_.size(), error);
				if (written < 0) {
					if (error != EAGAIN) {
						log(logmsg::error, _("Could not write to socket: %s"), fz::socket_error_description(error));
						log(logmsg::error, _("Disconnected from server"));
						return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
					}
					return FZ_REPLY_WOULDBLOCK;
				}
				else if (written) {
					controlSocket_.SetAlive();
					req.body_buffer_.consume(static_cast<size_t>(written));
					dataToSend_ -= written;
					if (req.flags_ & HttpRequest::flag_update_transferstatus) {
						engine_.transfer_status_.Update(written);
					}
				}
			}

			log(logmsg::debug_info, "Finished sending request body");

#if FZ_WINDOWS
			stop_timer(buffer_tuning_timer_);
#endif
			controlSocket_.socket_->set_flags(fz::socket::flag_nodelay, true);

			req.flags_ |= HttpRequest::flag_sent_body;

			sendLogLevel_ = logmsg::debug_verbose;

			opState &= ~request_send;
			++send_pos_;

			if (send_pos_ < requests_.size()) {
				if (!req.keep_alive()) {
					opState |= request_send_wait_for_read;
					log(logmsg::debug_info, L"Request did not ask for keep-alive. Waiting for response to finish before sending next request a new connection.");
				}
				else {
					opState |= request_init;
				}
			}

			return FZ_REPLY_CONTINUE;

		}
	}

	if (opState & request_reading) {
		return FZ_REPLY_WOULDBLOCK;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CHttpRequestOpData::SubcommandResult(int, COpData const&)
{
	if (opState & request_wait_connect) {
		opState &= ~request_wait_connect;
		opState |= request_send;
	}

	return FZ_REPLY_CONTINUE;
}

int CHttpRequestOpData::FinalizeResponseBody()
{
	auto & shared_response = requests_.front();
	if (shared_response) {
		auto & response = shared_response->response();
		if (!(response.flags_ & (HttpResponse::flag_ignore_body | HttpResponse::flag_no_body))) {
			response.flags_ |= HttpResponse::flag_got_body;
			if (response.success() && response.writer_) {
				auto r = response.writer_->finalize(read_state_.writer_buffer_);
				switch (r) {
				case aio_result::ok:
					return FZ_REPLY_OK;
				case aio_result::wait:
					return FZ_REPLY_WOULDBLOCK;
				default:
					return FZ_REPLY_ERROR;
				}
			}
		}
	}
	return FZ_REPLY_OK;
}

int CHttpRequestOpData::ParseReceiveBuffer()
{
	if (read_state_.done_) {
		return FinalizeResponseBody();
	}

	auto & shared_response = requests_.front();
	if (shared_response) {
		auto & request = shared_response->request();
		if (!(request.flags_ & HttpRequest::flag_sent_header)) {
			if (read_state_.eof_) {
				log(logmsg::debug_verbose, L"Socket closed before request headers got sent");
				log(logmsg::error, _("Connection closed by server"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			else if (!recv_buffer_.empty()) {
				log(logmsg::error, _("Server sent data even before request headers were sent"));
				return FZ_REPLY_ERROR;
			}
		}

		auto & response = shared_response->response();

		if (!response.got_header()) {
			int res = ParseHeader();
			if (read_state_.eof_ && res == (FZ_REPLY_WOULDBLOCK | FZ_REPLY_CONTINUE)) {
				log(logmsg::debug_verbose, L"Socket closed before headers got received");
				log(logmsg::error, _("Connection closed by server"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			return res;
		}
	}

	if (read_state_.transfer_encoding_ == chunked) {
		int res = ParseChunkedData();
		if (read_state_.eof_ && res == (FZ_REPLY_WOULDBLOCK | FZ_REPLY_CONTINUE)) {
			log(logmsg::debug_verbose, L"Socket closed, chunk incomplete");
			log(logmsg::error, _("Connection closed by server"));
			return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
		}
		return res;
	}
	else {
		size_t size = recv_buffer_.size();
		if (!size) {
			if (read_state_.eof_) {
				if (read_state_.responseContentLength_ != -1 && read_state_.receivedData_ != read_state_.responseContentLength_) {
					log(logmsg::debug_verbose, L"Socket closed, content length not reached");
					log(logmsg::error, _("Connection closed by server"));
					return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
				}
				else {
					read_state_.done_ = true;
					return FinalizeResponseBody();
				}
			}
			return FZ_REPLY_WOULDBLOCK|FZ_REPLY_CONTINUE;
		}

		if (read_state_.responseContentLength_ != -1 && read_state_.receivedData_ + static_cast<int64_t>(size) > read_state_.responseContentLength_) {
			size = static_cast<size_t>(read_state_.responseContentLength_ - read_state_.receivedData_);
		}

		int res = ProcessData(recv_buffer_.get(), size);
		recv_buffer_.consume(recv_buffer_.size() - size);
		return res;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CHttpRequestOpData::OnReceive(bool repeatedProcessing)
{
	while (controlSocket_.socket_) {
		if (repeatedProcessing) {
			repeatedProcessing = false;
		}
		else if (!read_state_.eof_) {
			int error;
			size_t const recv_size = 1024 * 64;
			int read = controlSocket_.active_layer_->read(recv_buffer_.get(recv_size), recv_size, error);
			if (read <= -1) {
				if (error != EAGAIN) {
					log(logmsg::error, _("Could not read from socket: %s"), fz::socket_error_description(error));
					return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
				}
				return FZ_REPLY_WOULDBLOCK;
			}
			else if (read) {
				recv_buffer_.add(static_cast<size_t>(read));
				controlSocket_.SetAlive();
			}

			read_state_.eof_ = read == 0;
		}

		while (!requests_.empty()) {
			int res = ParseReceiveBuffer();
			if (res == FZ_REPLY_WOULDBLOCK) {
				return res;
			}
			else if (res == (FZ_REPLY_WOULDBLOCK|FZ_REPLY_CONTINUE)) {
				break;
			}

			if (res == FZ_REPLY_OK) {
				log(logmsg::debug_info, L"Finished a response");
				if (requests_.front()) {
					if (requests_.front()->request().body_) {
						requests_.front()->request().body_->set_handler(nullptr);
					}
					if (requests_.front()->response().writer_) {
						requests_.front()->response().writer_->set_handler(nullptr);
					}
				}

				requests_.pop_front();

				bool premature{};
				if (!send_pos_) {
					premature = true;
				}
				else {
					--send_pos_;
				}

				bool const socket_done = read_state_.eof_ || !read_state_.keep_alive_ || premature;
				if (socket_done) {
					controlSocket_.active_layer_->shutdown();
					if (!recv_buffer_.empty()) {
						log(logmsg::debug_verbose, "Discarding %u bytes from receive buffer", recv_buffer_.size());
						recv_buffer_.clear();
					}

					controlSocket_.ResetSocket();
				}

				read_state_ = read_state();

				if (requests_.empty()) {
					log(logmsg::debug_info, L"Done reading last response");
					opState &= ~request_reading;

					if (!recv_buffer_.empty()) {
						log(logmsg::error, _("Malformed response: %s"), _("Server sent too much data."));
						return FZ_REPLY_ERROR;
					}
					return FZ_REPLY_OK;
				}

				if (socket_done) {
					send_pos_ = 0;
					opState = request_init | request_reading;
					return FZ_REPLY_CONTINUE;
				}
			}
			else if (res != FZ_REPLY_CONTINUE) {
				return res;
			}
		}

		if (requests_.empty() && !recv_buffer_.empty()) {
			log(logmsg::error, _("Malformed response: %s"), _("Server sent too much data."));
			return FZ_REPLY_ERROR;
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CHttpRequestOpData::ParseHeader()
{
	log(logmsg::debug_verbose, L"CHttpRequestOpData::ParseHeader()");

	// Parse the HTTP header.
	// We do just the neccessary parsing and silently ignore most header fields
	// The calling operation is responsible for things like redirect parsing.
	for (;;) {
		// Find line ending
		size_t i = 0;
		for (i = 0; (i + 1) < recv_buffer_.size(); ++i) {
			if (recv_buffer_[i] == '\r') {
				if (recv_buffer_[i + 1] != '\n') {
					log(logmsg::error, _("Malformed response header: %s"), _("Server not sending proper line endings"));
					return FZ_REPLY_ERROR;
				}
				break;
			}
			if (!recv_buffer_[i]) {
				log(logmsg::error, _("Malformed response header: %s"), _("Null character in line"));
				return FZ_REPLY_ERROR;
			}
		}
		if ((i + 1) >= recv_buffer_.size()) {
			size_t const max_line_size = 8192;
			if (recv_buffer_.size() >= max_line_size) {
				log(logmsg::error, _("Too long header line"));
				return FZ_REPLY_ERROR;
			}
			return FZ_REPLY_WOULDBLOCK|FZ_REPLY_CONTINUE;
		}

		std::wstring wline = fz::to_wstring_from_utf8(reinterpret_cast<char const*>(recv_buffer_.get()), i);
		if (wline.empty()) {
			wline = fz::to_wstring(std::string(recv_buffer_.get(), recv_buffer_.get() + i));
		}
		if (!wline.empty()) {
			controlSocket_.log_raw(logmsg::reply, wline);
		}

		auto & response = requests_.front()->response();
		if (!response.got_code()) {
			if (recv_buffer_.size() < 15 || memcmp(recv_buffer_.get(), "HTTP/1.", 7)) {
				// Invalid HTTP Status-Line
				log(logmsg::error, _("Invalid HTTP Response"));
				return FZ_REPLY_ERROR;
			}

			if (recv_buffer_[9] < '1' || recv_buffer_[9] > '5' ||
				recv_buffer_[10] < '0' || recv_buffer_[10] > '9' ||
				recv_buffer_[11] < '0' || recv_buffer_[11] > '9')
			{
				// Invalid response code
				log(logmsg::error, _("Invalid response code"));
				return FZ_REPLY_ERROR;
			}

			unsigned int code = response.code_ = (recv_buffer_[9] - '0') * 100 + (recv_buffer_[10] - '0') * 10 + recv_buffer_[11] - '0';
			if (code != 100) {
				response.code_ = code;
				response.flags_ |= HttpResponse::flag_got_code;
			}

			if (!send_pos_) {
				if (response.success()) {
					log(logmsg::error, _("Broken server, it claims to have processed a request before it got fully set"));
					return FZ_REPLY_ERROR;
				}
				else {
					log(logmsg::debug_info, "Premature error response");
				}
			}
		}
		else {
			if (!i) {
				recv_buffer_.consume(2);

				// End of header
				return ProcessCompleteHeader();
			}

			std::string line(recv_buffer_.get(), recv_buffer_.get() + i);

			auto delim_pos = line.find(':');
			if (delim_pos == std::string::npos || !delim_pos) {
				log(logmsg::error, _("Malformed response header: %s"), _("Invalid line"));
				return FZ_REPLY_ERROR;
			}

			std::string value;
			auto value_start = line.find_first_not_of(" \t", delim_pos + 1);
			if (value_start != std::string::npos) {
				int value_stop = line.find_last_not_of(" \t"); // Cannot fail
				value = line.substr(value_start, value_stop - value_start + 1);
			}

			auto & header = response.headers_[line.substr(0, delim_pos)];
			if (header.empty()) {
				header = value;
			}
			else if (!value.empty()) {
				header += ", " + value;
			}
		}

		recv_buffer_.consume(i + 2);

		if (recv_buffer_.empty()) {
			break;
		}
	}

	return FZ_REPLY_WOULDBLOCK|FZ_REPLY_CONTINUE;
}

int CHttpRequestOpData::ProcessCompleteHeader()
{
	log(logmsg::debug_verbose, L"CHttpRequestOpData::ProcessCompleteHeader()");

	auto & srr = requests_.front();
	auto & request = srr->request();
	auto & response = srr->response();

	response.flags_ |= HttpResponse::flag_got_header;
	if (request.verb_ == "HEAD" || response.code_prohobits_body()) {
		response.flags_ |= HttpResponse::flag_no_body;
	}

	auto const te = fz::str_tolower_ascii(response.get_header("Transfer-Encoding"));
	if (te == "chunked") {
		read_state_.transfer_encoding_ = chunked;
	}
	else if (te.empty() || te == "identity") {
		read_state_.transfer_encoding_ = identity;
	}
	else {
		log(logmsg::error, _("Malformed response header: %s"), _("Unknown transfer encoding"));
		return FZ_REPLY_ERROR;
	}

	auto retry = response.get_header("Retry-After");
	if (response.code_ >= 400 && !retry.empty()) {
		// TODO: Retry-After for redirects
		auto const now = fz::datetime::now();

		fz::duration d;
		int seconds = fz::to_integral<int>(retry, -1);
		if (seconds > 0) {
			d = fz::duration::from_seconds(seconds);
		}
		else {
			fz::datetime t;
			if (t.set_rfc822(retry)) {
				if (t > now) {
					d = t - now;
				}
			}
		}

		if (!d && response.code_ == 429) {
			d = fz::duration::from_seconds(1);
		}
		if (d) {
			log(logmsg::debug_verbose, "Got Retry-After with %d", d.get_seconds());
			controlSocket_.throttler_.throttle(request.uri_.host_, now + d);
		}
	}

	int64_t length{-1};
	auto const cl = response.get_header("Content-Length");
	if (!cl.empty()) {
		length = fz::to_integral<int64_t>(cl, -1);
		if (length < 0) {
			log(logmsg::error, _("Malformed response header: %s"), _("Invalid Content-Length"));
			return FZ_REPLY_ERROR;
		}
	}

	if (response.no_body()) {
		read_state_.responseContentLength_ = 0;
	}
	else {
		read_state_.responseContentLength_ = length;
	}

	read_state_.keep_alive_ = response.keep_alive() && request.keep_alive();

	int res = FZ_REPLY_CONTINUE;
	if (response.on_header_) {
		res = response.on_header_(srr);

		if (res == FZ_REPLY_CONTINUE) {
			if (response.writer_) {
				response.writer_->set_handler(this);
			}
		}
		else {
			if (res == FZ_REPLY_OK) {
				if (send_pos_) {
					// Clear the pointer, we no longer need the request to finish, all needed information is in read_state_
					if (request.body_) {
						request.body_->set_handler(nullptr);
					}
					srr.reset();
					res = FZ_REPLY_CONTINUE;
				}
			}
		}
	}

	if (res == FZ_REPLY_CONTINUE) {
		if (!read_state_.responseContentLength_) {
			read_state_.done_ = true;
			return FinalizeResponseBody();
		}
	}

	return res;
}

int CHttpRequestOpData::ParseChunkedData()
{
	while (!recv_buffer_.empty()) {
		if (read_state_.chunk_data_.size != 0) {
			size_t dataLen = recv_buffer_.size();
			if (read_state_.chunk_data_.size < recv_buffer_.size()) {
				dataLen = static_cast<size_t>(read_state_.chunk_data_.size);
			}
			size_t remaining = dataLen;
			int res = ProcessData(recv_buffer_.get(), remaining);
			recv_buffer_.consume(dataLen - remaining);
			read_state_.chunk_data_.size -= dataLen - remaining;

			if (res != FZ_REPLY_CONTINUE) {
				return res;
			}

			if (read_state_.chunk_data_.size == 0) {
				read_state_.chunk_data_.terminateChunk = true;
			}
		}

		// Find line ending
		size_t i = 0;
		for (i = 0; (i + 1) < recv_buffer_.size(); ++i) {
			if (recv_buffer_[i] == '\r') {
				if (recv_buffer_[i + 1] != '\n') {
					log(logmsg::error, _("Malformed chunk data: %s"), _("Wrong line endings"));
					return FZ_REPLY_ERROR;
				}
				break;
			}
			if (!recv_buffer_[i]) {
				log(logmsg::error, _("Malformed chunk data: %s"), _("Null character in line"));
				return FZ_REPLY_ERROR;
			}
		}
		if ((i + 1) >= recv_buffer_.size()) {
			size_t const max_line_size = 8192;
			if (recv_buffer_.size() >= max_line_size) {
				log(logmsg::error, _("Malformed chunk data: %s"), _("Line length exceeded"));
				return FZ_REPLY_ERROR;
			}
			break;
		}

		if (read_state_.chunk_data_.terminateChunk) {
			if (i) {
				// The chunk data has to end with CRLF. If i is nonzero,
				// it didn't end with just CRLF.
				log(logmsg::debug_debug, L"%u characters preceeding line-ending with value %s", i, fz::hex_encode<std::string>(std::string(recv_buffer_.get(), recv_buffer_.get() + recv_buffer_.size())));
				log(logmsg::error, _("Malformed chunk data: %s"), _("Chunk data improperly terminated"));
				return FZ_REPLY_ERROR;
			}
			read_state_.chunk_data_.terminateChunk = false;
		}
		else if (read_state_.chunk_data_.getTrailer) {
			if (!i) {
				// We're done
				recv_buffer_.consume(2);
				read_state_.done_ = true;
				return FinalizeResponseBody();
			}

			// Ignore the trailer
		}
		else {
			// Read chunk size
			unsigned char const* end = recv_buffer_.get() + i;
			for (unsigned char* q = recv_buffer_.get(); q != end && *q != ';' && *q != ' '; ++q) {
				read_state_.chunk_data_.size *= 16;
				if (*q >= '0' && *q <= '9') {
					read_state_.chunk_data_.size += *q - '0';
				}
				else if (*q >= 'A' && *q <= 'F') {
					read_state_.chunk_data_.size += *q - 'A' + 10;
				}
				else if (*q >= 'a' && *q <= 'f') {
					read_state_.chunk_data_.size += *q - 'a' + 10;
				}
				else {
					// Invalid size
					log(logmsg::error, _("Malformed chunk data: %s"), _("Invalid chunk size"));
					return FZ_REPLY_ERROR;
				}
			}
			if (!read_state_.chunk_data_.size) {
				read_state_.chunk_data_.getTrailer = true;
			}
		}

		recv_buffer_.consume(i + 2);
	}

	return FZ_REPLY_WOULDBLOCK|FZ_REPLY_CONTINUE;
}

int CHttpRequestOpData::ProcessData(unsigned char* data, size_t & remaining)
{
	int res = FZ_REPLY_CONTINUE;
	size_t initial = remaining;

	auto & shared_response = requests_.front();
	if (shared_response) {
		auto & response = shared_response->response();

		if (!(response.flags_ & HttpResponse::flag_ignore_body)) {
			if (response.success()) {
				if (response.writer_) {
					while (remaining) {
						if (read_state_.writer_buffer_.size() >= read_state_.writer_buffer_.capacity()) {
							auto r = response.writer_->get_write_buffer(read_state_.writer_buffer_);
							if (r == aio_result::wait) {
								res = FZ_REPLY_WOULDBLOCK;
								break;
							}
							else if (r == aio_result::error) {
								res =  FZ_REPLY_CRITICALERROR;
								break;
							}

							read_state_.writer_buffer_ = r.buffer_;
						}

						size_t s = std::min(remaining, read_state_.writer_buffer_.capacity() - read_state_.writer_buffer_.size());
						read_state_.writer_buffer_.append(data, s);
						data += s;
						remaining -= s;
					}
				}
				else {
					if (response.body_.size() < 1024*1024*16) {
						response.body_.append(data, remaining);
					}
					remaining = 0;
				}
			}
			else {
				if (response.body_.size() < 1024*1024*16) {
					response.body_.append(data, remaining);
				}
				remaining = 0;
			}
		}
		else {
			remaining = 0;
		}
	}
	else {
		remaining = 0;
	}

	read_state_.receivedData_ += initial - remaining;

	if (res == FZ_REPLY_CONTINUE && read_state_.receivedData_ == read_state_.responseContentLength_) {
		read_state_.done_ = true;
		res = FinalizeResponseBody();
	}

	return res;
}

int CHttpRequestOpData::Reset(int result)
{
	if (result != FZ_REPLY_OK) {
		controlSocket_.ResetSocket();
	}
	else if (opState != request_done) {
		controlSocket_.ResetSocket();
	}
	else if (!recv_buffer_.empty()) {
		log(logmsg::debug_verbose, L"Closing connection, the receive buffer isn't empty but at %d", recv_buffer_.size());
		controlSocket_.ResetSocket();
	}
	else {
		if (controlSocket_.active_layer_) {
			controlSocket_.send_event<fz::socket_event>(controlSocket_.active_layer_, fz::socket_event_flag::read, 0);
		}
	}

	return result;
}

void CHttpRequestOpData::operator()(fz::event_base const& ev)
{
	fz::dispatch<read_ready_event, write_ready_event, fz::timer_event>(ev, this, &CHttpRequestOpData::OnReaderReady, &CHttpRequestOpData::OnWriterReady, &CHttpRequestOpData::OnTimer);
}

void CHttpRequestOpData::OnReaderReady(reader_base * r)
{
	if (requests_.empty() || !requests_[send_pos_]) {
		return;
	}

	auto & rr = *requests_[send_pos_];
	auto & req = rr.request();
	if (req.body_.get() != r) {
		return;
	}
	if (req.flags_ & HttpRequest::flag_sent_header && !(req.flags_ & HttpRequest::flag_sent_body)) {
		controlSocket_.SendNextCommand();
	}
}

void CHttpRequestOpData::OnWriterReady(writer_base* writer)
{
	if (requests_.empty() || requests_.back()->response().writer_.get() != writer) {
		log(logmsg::debug_warning, L"Stale writer event");
		return;
	}

	int res = OnReceive(true);
	if (res == FZ_REPLY_CONTINUE) {
		controlSocket_.SendNextCommand();
	}
	else if (res != FZ_REPLY_WOULDBLOCK) {
		controlSocket_.ResetOperation(res);
	}
}

void CHttpRequestOpData::OnTimer(fz::timer_id)
{
#if FZ_WINDOWS
	if (controlSocket_.socket_ && controlSocket_.socket_->is_connected()) {
		int const ideal_send_buffer = controlSocket_.socket_->ideal_send_buffer_size();
		if (ideal_send_buffer != -1) {
			controlSocket_.socket_->set_buffer_sizes(-1, ideal_send_buffer);
		}
	}
#endif
}

