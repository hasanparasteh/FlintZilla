#include "../filezilla.h"
#include "../activity_logger_layer.h"
#include "../directorylistingparser.h"
#include "../engineprivate.h"
#include "../proxy.h"
#include "../servercapabilities.h"
#include "../tls.h"

#include "ftpcontrolsocket.h"
#include "transfersocket.h"

#include "../../include/engine_options.h"

#include <libfilezilla/rate_limited_layer.hpp>
#include <libfilezilla/util.hpp>

#ifndef FZ_WINDOWS
#define HAVE_ASCII_TRANSFORM 1
#endif

#if HAVE_ASCII_TRANSFORM
namespace {
class ascii_writer final : public writer_base, public fz::event_handler
{
public:
	ascii_writer(CFileZillaEnginePrivate& engine, fz::event_handler * handler, std::unique_ptr<writer_base> && writer)
		: writer_base(writer->name(), engine, handler, true)
		, fz::event_handler(engine.event_loop_)
		, writer_(std::move(writer))
	{
		writer_->set_handler(this);
	}

	~ascii_writer() {
		writer_.reset();
		remove_handler();
	}

	virtual uint64_t size() const override
	{
		return writer_->size();
	}

	virtual get_write_buffer_result get_write_buffer(fz::nonowning_buffer & last_written) override
	{
		transform(last_written);
		get_write_buffer_result r = writer_->get_write_buffer(last_written);
		if (r.type_ == aio_result::ok) {
			if (was_cr_) {
				r.buffer_.append('\r');
				was_cr_ = false;
			}
		}
		return r;
	}

	virtual aio_result finalize(fz::nonowning_buffer & last_written) override
	{
		transform(last_written);
		if (was_cr_) {
			last_written.append('\r');
			was_cr_ = false;
		}
		return writer_->finalize(last_written);
	}

private:
	void transform(fz::nonowning_buffer & b)
	{
		if (!b.empty()) {
			auto * const start = b.get();
			auto *p = start;
			auto *q = start;
			auto * const end = start + b.size();
			while (p != end) {
				auto c = *(p++);
				if (c == '\r') {
					was_cr_ = true;
				}
				else if (c == '\n') {
					was_cr_ = false;
					*(q++) = c;
				}
				else {
					if (was_cr_) {
						*(q++) = '\r';
						was_cr_ = false;
					}
					*(q++) = c;
				}
			}
			b.resize(q - start);
		}
	}

	std::unique_ptr<writer_base> writer_;

	virtual void operator()(fz::event_base const&) override
	{
		if (handler_) {
			handler_->operator()(write_ready_event(this));
		}
	}

	bool was_cr_{};
};

class ascii_reader final : public reader_base, public fz::event_handler
{
public:
	ascii_reader(CFileZillaEnginePrivate& engine, fz::event_handler * handler, std::unique_ptr<reader_base> && reader)
		: reader_base(reader->name(), engine, handler)
		, fz::event_handler(engine.event_loop_)
		, reader_(std::move(reader))
	{
		reader_->set_handler(this);
		size_ = reader_->size();
	}

	~ascii_reader()
	{
		reader_.reset();
		remove_handler();
	}

	virtual aio_result seek(uint64_t, uint64_t = aio_base::nosize) override
	{
		return aio_result::error;
	}

	virtual read_result read() override
	{
		read_result ret = reader_->read();
		if (ret.type_ != aio_result::ok) {
			return ret;
		}


		buffer_.clear();

		// In the worst case, length will doubled: If reading
		// only LFs from the file
		auto * q = buffer_.get(ret.buffer_.size() * 2);

		auto const * p = ret.buffer_.get();
		auto const * end = ret.buffer_.get() + ret.buffer_.size();


		// Convert all stand-alone LFs into CRLF pairs.
		while (p != end) {
			auto const& c = *(p++);
			if (c == '\n') {
				if (!was_cr_) {
					*(q++) = '\r';
				}
				was_cr_ = false;
			}
			else if (c == '\r') {
				was_cr_ = true;
			}
			else {
				was_cr_ = false;
			}

			*(q++) = c;
		}

		buffer_.add(q - buffer_.get());
		ret.buffer_ = fz::nonowning_buffer(buffer_.get(), buffer_.capacity(), buffer_.size());
		return ret;
	}

	std::unique_ptr<reader_base> reader_;

	virtual void operator()(fz::event_base const&) override
	{
		if (handler_) {
			handler_->operator()(read_ready_event(this));
		}
	}

private:
	fz::buffer buffer_;
	bool was_cr_{};
};
}
#endif

CTransferSocket::CTransferSocket(CFileZillaEnginePrivate & engine, CFtpControlSocket & controlSocket, TransferMode transferMode)
: fz::event_handler(controlSocket.event_loop_)
, engine_(engine)
, controlSocket_(controlSocket)
, m_transferMode(transferMode)
{
}

CTransferSocket::~CTransferSocket()
{
	remove_handler();
	if (m_transferEndReason == TransferEndReason::none) {
		m_transferEndReason = TransferEndReason::successful;
	}
	ResetSocket();
	
	reader_.reset();
	writer_.reset();
}

void CTransferSocket::set_reader(std::unique_ptr<reader_base> && reader, bool ascii)
{
#if HAVE_ASCII_TRANSFORM
	if (ascii) {
		reader_ = std::make_unique<ascii_reader>(engine_, this, std::move(reader));
	}
	else
#endif
	{
		(void)ascii;
		reader_ = std::move(reader);
		reader_->set_handler(this);
	}
}

void CTransferSocket::set_writer(std::unique_ptr<writer_base> && writer, bool ascii)
{
#if HAVE_ASCII_TRANSFORM
	if (ascii) {
		writer_ = std::make_unique<ascii_writer>(engine_, this, std::move(writer));
	}
	else
#endif
	{
		(void)ascii;
		writer_ = std::move(writer);
		writer_->set_handler(this);
	}
}

void CTransferSocket::ResetSocket()
{
	socketServer_.reset();

	active_layer_ = nullptr;

	tls_layer_.reset();
	proxy_layer_.reset();
	ratelimit_layer_.reset();
	activity_logger_layer_.reset();
	socket_.reset();
	buffer_.reset();
}

std::wstring CTransferSocket::SetupActiveTransfer(std::string const& ip)
{
	ResetSocket();
	socketServer_ = CreateSocketServer();

	if (!socketServer_) {
		controlSocket_.log(logmsg::debug_warning, L"CreateSocketServer failed");
		return std::wstring();
	}

	int error;
	int port = socketServer_->local_port(error);
	if (port == -1)	{
		ResetSocket();

		controlSocket_.log(logmsg::debug_warning, L"GetLocalPort failed: %s", fz::socket_error_description(error));
		return std::wstring();
	}

	if (engine_.GetOptions().get_int(OPTION_LIMITPORTS)) {
		port += static_cast<int>(engine_.GetOptions().get_int(OPTION_LIMITPORTS_OFFSET));
		if (port <= 0 || port >= 65536) {
			controlSocket_.log(logmsg::debug_warning, L"Port outside valid range");
			return std::wstring();
		}
	}

	std::wstring portArguments;
	if (socketServer_->address_family() == fz::address_type::ipv6) {
		portArguments = fz::sprintf(L"|2|%s|%d|", ip, port);
	}
	else {
		portArguments = fz::to_wstring(ip);
		fz::replace_substrings(portArguments, L".", L",");
		portArguments += fz::sprintf(L",%d,%d", port / 256, port % 256);
	}

	return portArguments;
}

void CTransferSocket::OnSocketEvent(fz::socket_event_source* source, fz::socket_event_flag t, int error)
{
	if (socketServer_) {
		if (t == fz::socket_event_flag::connection) {
			OnAccept(error);
		}
		else {
			controlSocket_.log(logmsg::debug_info, L"Unhandled socket event %d from listening socket", t);
		}
		return;
	}

	switch (t)
	{
	case fz::socket_event_flag::connection:
		if (error) {
			if (source == proxy_layer_.get()) {
				controlSocket_.log(logmsg::error, _("Proxy handshake failed: %s"), fz::socket_error_description(error));
			}
			else {
				controlSocket_.log(logmsg::error, _("The data connection could not be established: %s"), fz::socket_error_description(error));
			}
			TransferEnd(TransferEndReason::transfer_failure);
		}
		else {
			OnConnect();
		}
		break;
	case fz::socket_event_flag::read:
		if (error) {
			OnSocketError(error);
		}
		else {
			OnReceive();
		}
		break;
	case fz::socket_event_flag::write:
		if (error) {
			OnSocketError(error);
		}
		else {
			OnSend();
		}
		break;
	default:
		// Uninteresting
		break;
	}
}

void CTransferSocket::OnAccept(int error)
{
	controlSocket_.SetAlive();
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnAccept(%d)", error);

	if (!socketServer_) {
		controlSocket_.log(logmsg::debug_warning, L"No socket server in OnAccept", error);
		return;
	}

	socket_ = socketServer_->accept(error);
	if (!socket_) {
		if (error == EAGAIN) {
			controlSocket_.log(logmsg::debug_verbose, L"No pending connection");
		}
		else {
			controlSocket_.log(logmsg::status, _("Could not accept connection: %s"), fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
		return;
	}
	socketServer_.reset();

	if (!InitLayers(true)) {
		TransferEnd(TransferEndReason::transfer_failure);
		return;
	}

	if (active_layer_->get_state() == fz::socket_state::connected) {
		OnConnect();
	}
}

void CTransferSocket::OnConnect()
{
	controlSocket_.SetAlive();
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnConnect");

	if (!socket_) {
		controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnConnect called without socket");
		return;
	}

	if (tls_layer_) {
		auto const cap = CServerCapabilities::GetCapability(controlSocket_.currentServer_, tls_resumption);
		if (tls_layer_->resumed_session()) {
			if (cap != yes) {
				engine_.AddNotification(std::make_unique<FtpTlsResumptionNotification>(controlSocket_.currentServer_));
				CServerCapabilities::SetCapability(controlSocket_.currentServer_, tls_resumption, yes);
			}
		}
		else {
			if (cap == yes) {
				TransferEnd(TransferEndReason::failed_tls_resumption);
				return;
			}
			else if (cap == unknown) {
				// Ask whether to allow this insecure connection
				++activity_block_;
				controlSocket_.SendAsyncRequest(std::make_unique<FtpTlsNoResumptionNotification>(controlSocket_.currentServer_));
			}
		}
		// Re-enable Nagle algorithm
		socket_->set_flags(fz::socket::flag_nodelay, false);
	}

#ifdef FZ_WINDOWS
	if (m_transferMode == TransferMode::upload) {
		// For send buffer tuning
		add_timer(fz::duration::from_seconds(1), false);
	}
#endif

	if (!activity_block_) {
		TriggerPostponedEvents();
	}

	OnSend();
}

void CTransferSocket::OnReceive()
{
	controlSocket_.log(logmsg::debug_debug, L"CTransferSocket::OnReceive(), m_transferMode=%d", m_transferMode);

	if (activity_block_) {
		controlSocket_.log(logmsg::debug_verbose, L"Postponing receive, m_bActive was false.");
		m_postponedReceive = true;
		return;
	}

	if (m_transferEndReason == TransferEndReason::none) {
		if (m_transferMode == TransferMode::list) {
			// See comment in download loop
			for (int i = 0; i < 100; ++i) {
				char *pBuffer = new char[4096];
				int error;
				int numread = active_layer_->read(pBuffer, 4096, error);
				if (numread < 0) {
					delete [] pBuffer;
					if (error != EAGAIN) {
						controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
						TransferEnd(TransferEndReason::transfer_failure);
					}
					return;
				}

				if (numread > 0) {
					if (!m_pDirectoryListingParser->AddData(pBuffer, numread)) {
						TransferEnd(TransferEndReason::transfer_failure);
						return;
					}

					controlSocket_.SetAlive();
					if (!m_madeProgress) {
						m_madeProgress = 2;
						engine_.transfer_status_.SetMadeProgress();
					}
					engine_.transfer_status_.Update(numread);
				}
				else {
					delete [] pBuffer;
					TransferEnd(TransferEndReason::successful);
					return;
				}
			}
			send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::read, 0);
			return;
		}
		else if (m_transferMode == TransferMode::download) {
			int error;
			int numread;

			// Only do a certain number of iterations in one go to keep the event loop going.
			// Otherwise this behaves like a livelock on very large files written to a very fast
			// SSD downloaded from a very fast server.
			for (int i = 0; i < 100; ++i) {
				if (!CheckGetNextWriteBuffer()) {
					return;
				}

				size_t to_read = buffer_.capacity() - buffer_.size();
				numread = active_layer_->read(buffer_.get(to_read), static_cast<unsigned int>(to_read), error);
				if (numread <= 0) {
					break;
				}

				controlSocket_.SetAlive();
				if (!m_madeProgress) {
					m_madeProgress = 2;
					engine_.transfer_status_.SetMadeProgress();
				}

				buffer_.add(static_cast<size_t>(numread));
			}

			if (numread < 0) {
				if (error != EAGAIN) {
					controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
					TransferEnd(TransferEndReason::transfer_failure);
				}
			}
			else if (!numread) {
				FinalizeWrite();
			}
			else {
				send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::read, 0);
			}
			return;
		}
		else if (m_transferMode == TransferMode::resumetest) {
			for (;;) {
				char tmp[2];
				int error;
				int numread = active_layer_->read(tmp, 2, error);
				if (numread < 0) {
					if (error != EAGAIN) {
						controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
						TransferEnd(TransferEndReason::transfer_failure);
					}
					return;
				}

				if (!numread) {
					if (resumetest_ == 1) {
						TransferEnd(TransferEndReason::successful);
					}
					else {
						controlSocket_.log(logmsg::debug_warning, L"Server incorrectly sent %d bytes", resumetest_);
						TransferEnd(TransferEndReason::failed_resumetest);
					}
					return;
				}
				resumetest_ += numread;

				if (resumetest_ > 1) {
					controlSocket_.log(logmsg::debug_warning, L"Server incorrectly sent %d bytes", resumetest_);
					TransferEnd(TransferEndReason::failed_resumetest);
					return;
				}
			}
			return;
		}
	}

	char discard[1024];
	int error;
	int numread = active_layer_->read(discard, 1024, error);

	if (m_transferEndReason == TransferEndReason::none) {
		// If we get here we're uploading

		if (numread > 0) {
			controlSocket_.log(logmsg::error, L"Received data from the server during an upload");
			TransferEnd(TransferEndReason::transfer_failure);
		}
		else if (numread < 0 && error != EAGAIN) {
			controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
	}
	else {
		if (!numread || (numread < 0 && error != EAGAIN)) {
			ResetSocket();
		}
	}
}

void CTransferSocket::OnSend()
{
	if (!active_layer_) {
		controlSocket_.log(logmsg::debug_verbose, L"OnSend called without backend. Ignoring event.");
		return;
	}

	if (activity_block_) {
		controlSocket_.log(logmsg::debug_verbose, L"Postponing send");
		m_postponedSend = true;
		return;
	}

	if (m_transferMode != TransferMode::upload || m_transferEndReason != TransferEndReason::none) {
		return;
	}

	int error;
	int written;

	// Only do a certain number of iterations in one go to keep the event loop going.
	// Otherwise this behaves like a livelock on very large files read from a very fast
	// SSD uploaded to a very fast server.
	for (int i = 0; i < 100; ++i) {
		if (!CheckGetNextReadBuffer()) {
			return;
		}

		written = active_layer_->write(buffer_.get(), static_cast<int>(buffer_.size()), error);
		if (written <= 0) {
			break;
		}

		controlSocket_.SetAlive();
		if (m_madeProgress == 1) {
			controlSocket_.log(logmsg::debug_debug, L"Made progress in CTransferSocket::OnSend()");
			m_madeProgress = 2;
			engine_.transfer_status_.SetMadeProgress();
		}
		engine_.transfer_status_.Update(written);

		buffer_.consume(written);
	}

	if (written < 0) {
		if (error == EAGAIN) {
			if (!m_madeProgress) {
				controlSocket_.log(logmsg::debug_debug, L"First EAGAIN in CTransferSocket::OnSend()");
				m_madeProgress = 1;
				engine_.transfer_status_.SetMadeProgress();
			}
		}
		else {
			controlSocket_.log(logmsg::error, L"Could not write to transfer socket: %s", fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
	}
	else if (written > 0) {
		send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::write, 0);
	}
}

void CTransferSocket::OnSocketError(int error)
{
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnSocketError(%d)", error);

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	controlSocket_.log(logmsg::error, _("Transfer connection interrupted: %s"), fz::socket_error_description(error));
	TransferEnd(TransferEndReason::transfer_failure);
}

bool CTransferSocket::SetupPassiveTransfer(std::wstring const& host, int port)
{
	std::string ip = fz::to_utf8(host);

	ResetSocket();

	socket_ = std::make_unique<fz::socket>(engine_.GetThreadPool(), nullptr);

	SetSocketBufferSizes(*socket_);

	// Try to bind the source IP of the data connection to the same IP as the control connection.
	// We can do so either if
	// 1) the destination IP of the data connection matches peer IP of the control connection or
	// 2) we are using a proxy.
	//
	// In case destination IPs of control and data connection are different, do not bind to the
	// same source.

	std::string bindAddress;
	if (controlSocket_.proxy_layer_) {
		bindAddress = controlSocket_.socket_->local_ip();
		controlSocket_.log(logmsg::debug_info, L"Binding data connection source IP to control connection source IP %s", bindAddress);
		socket_->bind(bindAddress);
	}
	else {
		if (controlSocket_.socket_->peer_ip(true) == ip || controlSocket_.socket_->peer_ip(false) == ip) {
			bindAddress = controlSocket_.socket_->local_ip();
			controlSocket_.log(logmsg::debug_info, L"Binding data connection source IP to control connection source IP %s", bindAddress);
			socket_->bind(bindAddress);
		}
		else {
			controlSocket_.log(logmsg::debug_warning, L"Destination IP of data connection does not match peer IP of control connection. Not binding source address of data connection.");
		}
	}

	if (!InitLayers(false)) {
		ResetSocket();
		return false;
	}

	int res = active_layer_->connect(fz::to_native(ip), port, fz::address_type::unknown);
	if (res) {
		ResetSocket();
		return false;
	}

	return true;
}

bool CTransferSocket::InitLayers(bool active)
{
	activity_logger_layer_ = std::make_unique<activity_logger_layer>(nullptr, *socket_, engine_.activity_logger_);
	ratelimit_layer_ = std::make_unique<fz::rate_limited_layer>(nullptr, *activity_logger_layer_, &engine_.GetRateLimiter());
	active_layer_ = ratelimit_layer_.get();

	if (controlSocket_.proxy_layer_ && !active) {
		fz::native_string proxy_host = controlSocket_.proxy_layer_->next().peer_host();
		int error;
		int proxy_port = controlSocket_.proxy_layer_->next().peer_port(error);

		if (proxy_host.empty() || proxy_port < 1) {
			controlSocket_.log(logmsg::debug_warning, L"Could not get peer address of control connection.");
			return false;
		}

		proxy_layer_ = std::make_unique<CProxySocket>(nullptr, *active_layer_, &controlSocket_, controlSocket_.proxy_layer_->GetProxyType(), proxy_host, proxy_port, controlSocket_.proxy_layer_->GetUser(), controlSocket_.proxy_layer_->GetPass());
		active_layer_ = proxy_layer_.get();
	}

	if (controlSocket_.m_protectDataChannel) {
		// Disable Nagle's algorithm during TLS handshake
		socket_->set_flags(fz::socket::flag_nodelay, true);

		tls_layer_ = std::make_unique<fz::tls_layer>(controlSocket_.event_loop_, nullptr, *active_layer_, nullptr, controlSocket_.logger_);
		active_layer_ = tls_layer_.get();
		tls_layer_->set_min_tls_ver(get_min_tls_ver(engine_.GetOptions()));

		if (!tls_layer_->client_handshake(controlSocket_.tls_layer_->get_raw_certificate(), controlSocket_.tls_layer_->get_session_parameters(), controlSocket_.tls_layer_->peer_host())) {
			return false;
		}
	}

	active_layer_->set_event_handler(this);

	return true;
}

void CTransferSocket::SetActive()
{
	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (!activity_block_) {
		return;
	}
	--activity_block_;
	if (!socket_) {
		return;
	}

	if (socket_->is_connected()) {
		TriggerPostponedEvents();
	}
}

void CTransferSocket::TransferEnd(TransferEndReason reason)
{
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::TransferEnd(%d)", reason);

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}
	m_transferEndReason = reason;

	if (reason != TransferEndReason::successful) {
		ResetSocket();
	}
	else {
		active_layer_->shutdown();
	}

	controlSocket_.send_event<TransferEndEvent>();
}

std::unique_ptr<fz::listen_socket> CTransferSocket::CreateSocketServer(int port)
{
	auto socket = std::make_unique<fz::listen_socket>(engine_.GetThreadPool(), this);
	int res = socket->listen(controlSocket_.socket_->address_family(), port);
	if (res) {
		controlSocket_.log(logmsg::debug_verbose, L"Could not listen on port %d: %s", port, fz::socket_error_description(res));
		socket.reset();
	}
	else {
		SetSocketBufferSizes(*socket);
	}

	return socket;
}

std::unique_ptr<fz::listen_socket> CTransferSocket::CreateSocketServer()
{
	if (!engine_.GetOptions().get_int(OPTION_LIMITPORTS)) {
		// Ask the systen for a port
		return CreateSocketServer(0);
	}

	// Try out all ports in the port range.
	// Upon first call, we try to use a random port fist, after that
	// increase the port step by step

	// Windows only: I think there's a bug in the socket implementation of
	// Windows: Even if using SO_REUSEADDR, using the same local address
	// twice will fail unless there are a couple of minutes between the
	// connection attempts. This may cause problems if transferring lots of
	// files with a narrow port range.

	static int start = 0;

	int low = engine_.GetOptions().get_int(OPTION_LIMITPORTS_LOW);
	int high = engine_.GetOptions().get_int(OPTION_LIMITPORTS_HIGH);
	if (low > high) {
		low = high;
	}

	if (start < low || start > high) {
		start = static_cast<decltype(start)>(fz::random_number(low, high));
	}

	std::unique_ptr<fz::listen_socket> server;

	int count = high - low + 1;
	while (count--) {
		server = CreateSocketServer(start++);
		if (server) {
			break;
		}
		if (start > high) {
			start = low;
		}
	}

	return server;
}

bool CTransferSocket::CheckGetNextWriteBuffer()
{
	if (buffer_.size() >= buffer_.capacity()) {
		auto res = writer_->get_write_buffer(buffer_);
		if (res == aio_result::wait) {
			return false;
		}
		else if (res == aio_result::error) {
			TransferEnd(TransferEndReason::transfer_failure_critical);
			return false;
		}

		buffer_ = res.buffer_;
	}

	return true;
}

bool CTransferSocket::CheckGetNextReadBuffer()
{
	if (buffer_.empty()) {
		read_result res = reader_->read();
		if (res == aio_result::wait) {
			return false;
		}
		else if (res == aio_result::error) {
			TransferEnd(TransferEndReason::transfer_failure_critical);
			return false;
		}

		buffer_ = res.buffer_;
		if (buffer_.empty()) {
			int r = active_layer_->shutdown();
			if (r && r != EAGAIN) {
				TransferEnd(TransferEndReason::transfer_failure);
				return false;
			}
			TransferEnd(TransferEndReason::successful);
			return false;
		}
	}

	return true;
}

void CTransferSocket::OnInput(reader_base*)
{
	if (activity_block_ || m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (m_transferMode == TransferMode::upload) {
		OnSend();
	}
}

void CTransferSocket::OnWrite(writer_base*)
{
	if (activity_block_ || m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (m_transferMode == TransferMode::download) {
		OnReceive();
	}
}

void CTransferSocket::FinalizeWrite()
{
	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	auto res = writer_->finalize(buffer_);
	if (res == aio_result::wait) {
		return;
	}

	if (res == aio_result::ok) {
		TransferEnd(TransferEndReason::successful);
	}
	else {
		TransferEnd(TransferEndReason::transfer_failure_critical);
	}

}

void CTransferSocket::TriggerPostponedEvents()
{
	if (activity_block_) {
		return;
	}

	if (m_postponedReceive) {
		controlSocket_.log(logmsg::debug_verbose, L"Executing postponed receive");
		m_postponedReceive = false;
		OnReceive();
		if (m_transferEndReason != TransferEndReason::none) {
			return;
		}
	}
	if (m_postponedSend) {
		controlSocket_.log(logmsg::debug_verbose, L"Executing postponed send");
		m_postponedSend = false;
		OnSend();
	}
}

void CTransferSocket::SetSocketBufferSizes(fz::socket_base& socket)
{
	const int size_read = engine_.GetOptions().get_int(OPTION_SOCKET_BUFFERSIZE_RECV);
#if FZ_WINDOWS
	const int size_write = -1;
#else
	const int size_write = engine_.GetOptions().get_int(OPTION_SOCKET_BUFFERSIZE_SEND);
#endif
	socket.set_buffer_sizes(size_read, size_write);
}

void CTransferSocket::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::socket_event, read_ready_event, write_ready_event, fz::timer_event>(ev, this,
		&CTransferSocket::OnSocketEvent,
		&CTransferSocket::OnInput,
		&CTransferSocket::OnWrite,
		&CTransferSocket::OnTimer);
}

void CTransferSocket::OnTimer(fz::timer_id)
{
#if FZ_WINDOWS
	if (socket_ && socket_->is_connected()) {
		int const ideal_send_buffer = socket_->ideal_send_buffer_size();
		if (ideal_send_buffer != -1) {
			socket_->set_buffer_sizes(-1, ideal_send_buffer);
		}
	}
#endif
}

void CTransferSocket::ContinueWithoutSesssionResumption()
{
	if (activity_block_) {
		--activity_block_;
		TriggerPostponedEvents();
	}
}
