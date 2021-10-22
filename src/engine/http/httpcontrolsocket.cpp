#include "../filezilla.h"

#include "connect.h"
#include "filetransfer.h"
#include "httpcontrolsocket.h"
#include "internalconnect.h"
#include "request.h"

#include "../../include/engine_options.h"

#include "../controlsocket.h"
#include "../engineprivate.h"
#include "../tls.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/uri.hpp>

#include <assert.h>
#include <string.h>

uint64_t HttpRequest::update_content_length()
{
	uint64_t ret{};
	if (body_) {
		ret = body_->size();
		if (ret != aio_base::nosize) {
			headers_["Content-Length"] = fz::to_string(ret);
		}
		else {
			headers_["Content-Length"] = "0";
		}
	}
	else {
		if (verb_ == "GET" || verb_ == "HEAD" || verb_ == "OPTIONS") {
			headers_.erase("Content-Length");
		}
		else {
			headers_["Content-Length"] = "0";
		}
	}
	return ret;
}

int HttpRequest::reset()
{
	flags_ &= (flag_update_transferstatus | flag_confidential_querystring);

	if (body_) {
		aio_result res = body_->rewind();
		if (res != aio_result::ok) {
			return FZ_REPLY_ERROR;
		}
		body_buffer_ = fz::nonowning_buffer();
	}

	return FZ_REPLY_CONTINUE;
}

int HttpResponse::reset()
{
	flags_ = 0;
	code_ = 0;
	headers_.clear();
	body_.clear();

	return FZ_REPLY_CONTINUE;
}

void RequestThrottler::throttle(std::string const& hostname, fz::datetime const& backoff)
{
	if (hostname.empty() || !backoff) {
		return;
	}

	fz::scoped_lock l(mtx_);

	bool found{};
	auto now = fz::datetime::now();
	for (size_t i = 0; i < backoff_.size(); ) {
		auto & entry = backoff_[i];
		if (entry.first == hostname) {
			found = true;
			if (entry.second < backoff) {
				entry.second = backoff;
			}
		}
		if (entry.second < now) {
			backoff_[i] = std::move(backoff_.back());
			backoff_.pop_back();
		}
		else {
			++i;
		}
	}
	if (!found) {
		backoff_.emplace_back(hostname, backoff);
	}
}

fz::duration RequestThrottler::get_throttle(std::string const& hostname)
{
	fz::scoped_lock l(mtx_);

	fz::duration ret;

	auto now = fz::datetime::now();
	for (size_t i = 0; i < backoff_.size(); ) {
		auto & entry = backoff_[i];
		if (entry.second < now) {
			backoff_[i] = std::move(backoff_.back());
			backoff_.pop_back();
		}
		else {
			if (entry.first == hostname) {
				ret = entry.second - now;
			}
			++i;
		}
	}

	return ret;
}

RequestThrottler CHttpControlSocket::throttler_;

CHttpControlSocket::CHttpControlSocket(CFileZillaEnginePrivate & engine)
	: CRealControlSocket(engine)
{
}

CHttpControlSocket::~CHttpControlSocket()
{
	remove_handler();
	DoClose();
}

bool CHttpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::SetAsyncRequestReply");

	switch (pNotification->GetRequestID())
	{
	case reqId_fileexists:
		{
			if (operations_.back()->opId != Command::transfer) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %f", pNotification->GetRequestID());
				return false;
			}

			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
		break;
	case reqId_certificate:
		{
			if (!tls_layer_ || tls_layer_->get_state() != fz::socket_state::connecting) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			CCertificateNotification* pCertificateNotification = static_cast<CCertificateNotification *>(pNotification);
			tls_layer_->set_verification_result(pCertificateNotification->trusted_);
		}
		break;
	default:
		log(logmsg::debug_warning, L"Unknown request %d", pNotification->GetRequestID());
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}


void CHttpControlSocket::OnReceive()
{
	if (operations_.empty() || operations_.back()->opId != PrivCommand::http_request) {
		uint8_t buffer;
		int error{};
		int read = active_layer_->read(&buffer, 1, error);
		if (!read) {
			log(logmsg::debug_warning, L"Idle socket got closed");
			ResetSocket();
		}
		else if (read == -1) {
			if (error != EAGAIN) {
				log(logmsg::debug_warning, L"OnReceive called while not processing http request. Reading fails with error %d, closing socket.", error);
				ResetSocket();
			}
		}
		else if (read) {
			log(logmsg::debug_warning, L"Server sent data while not in an active HTTP request, closing socket.");
			ResetSocket();
		}
		return;
	}

	int res = static_cast<CHttpRequestOpData&>(*operations_.back()).OnReceive(false);
	if (res == FZ_REPLY_CONTINUE) {
		SendNextCommand();
	}
	else if (res != FZ_REPLY_WOULDBLOCK) {
		ResetOperation(res);
	}
}

void CHttpControlSocket::OnConnect()
{
	if (operations_.empty() || operations_.back()->opId != PrivCommand::http_connect) {
		log(logmsg::debug_warning, L"Discarding stale OnConnect");
		return;
	}

	socket_->set_flags(fz::socket::flag_nodelay, true);

	auto & data = static_cast<CHttpInternalConnectOpData &>(*operations_.back());

	if (data.tls_) {
		if (!tls_layer_) {
			log(logmsg::status, _("Connection established, initializing TLS..."));

			tls_layer_ = std::make_unique<fz::tls_layer>(event_loop_, this, *active_layer_, &engine_.GetContext().GetTlsSystemTrustStore(), logger_);
			active_layer_ = tls_layer_.get();

			tls_layer_->set_alpn("http/1.1");
			if (!tls_layer_->client_handshake(&data)) {
				tls_layer_->set_min_tls_ver(get_min_tls_ver(engine_.GetOptions()));
				DoClose();
			}
		}
		else {
			log(logmsg::status, _("TLS connection established, sending HTTP request"));
			ResetOperation(FZ_REPLY_OK);
		}
	}
	else {
		log(logmsg::status, _("Connection established, sending HTTP request"));
		ResetOperation(FZ_REPLY_OK);
	}
}

void CHttpControlSocket::FileTransfer(CFileTransferCommand const& cmd)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::FileTransfer()");

	if (cmd.GetFlags() & transfer_flags::download) {
		log(logmsg::status, _("Downloading %s"), cmd.GetRemotePath().FormatFilename(cmd.GetRemoteFile()));
	}

	Push(std::make_unique<CHttpFileTransferOpData>(*this, cmd));
}

void CHttpControlSocket::FileTransfer(CHttpRequestCommand const& cmd)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::FileTransfer()");

	log(logmsg::status, _("Requesting %s"), cmd.uri_.to_string(!cmd.confidential_qs_));

	Push(std::make_unique<CHttpFileTransferOpData>(*this, cmd));
}

void CHttpControlSocket::Request(std::shared_ptr<HttpRequestResponseInterface> const& request)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::Request()");

	if (!request) {
		log(logmsg::debug_warning, L"Dropping null request");
		return;
	}

	auto op = dynamic_cast<CHttpRequestOpData*>(operations_.empty() ? nullptr : operations_.back().get());
	if (op) {
		op->AddRequest(request);
	}
	else {
		Push(std::make_unique<CHttpRequestOpData>(*this, request));
	}
}

void CHttpControlSocket::Request(std::deque<std::shared_ptr<HttpRequestResponseInterface>> && requests)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::Request()");
	Push(std::make_unique<CHttpRequestOpData>(*this, std::move(requests)));
}

int CHttpControlSocket::InternalConnect(std::wstring const& host, unsigned short port, bool tls, bool allowDisconnect)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::InternalConnect()");

	if (!currentServer_) {
		return FZ_REPLY_INTERNALERROR;
	}

	if (active_layer_) {
		if (host == connected_host_ && port == connected_port_ && tls == connected_tls_) {
			log(logmsg::debug_verbose, L"Reusing an existing connection");
			return FZ_REPLY_OK;
		}
		if (!allowDisconnect) {
			return FZ_REPLY_WOULDBLOCK;
		}
	}

	ResetSocket();
	connected_host_ = host;
	connected_port_ = port;
	connected_tls_ = tls;
	Push(std::make_unique<CHttpInternalConnectOpData>(*this, ConvertDomainName(host), port, tls));

	return FZ_REPLY_CONTINUE;
}

void CHttpControlSocket::OnSocketError(int error)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::OnClose(%d)", error);

	if (operations_.empty() || (operations_.back()->opId != PrivCommand::http_connect && operations_.back()->opId != PrivCommand::http_request)) {
		log(logmsg::debug_warning, L"Idle socket got closed");
		ResetSocket();
		return;
	}

	log(logmsg::error, _("Disconnected from server: %s"), fz::socket_error_description(error));
	ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
}

void CHttpControlSocket::ResetSocket()
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::ResetSocket()");

	active_layer_ = nullptr;

	tls_layer_.reset();

	CRealControlSocket::ResetSocket();
}

int CHttpControlSocket::Disconnect()
{
	DoClose();
	return FZ_REPLY_OK;
}

void CHttpControlSocket::Connect(CServer const& server, Credentials const& credentials)
{
	currentServer_ = server;
	credentials_ = credentials;
	Push(std::make_unique<CHttpConnectOpData>(*this));
}

int CHttpControlSocket::OnSend()
{
	int res = CRealControlSocket::OnSend();
	if (res == FZ_REPLY_CONTINUE) {
		if (!operations_.empty() && operations_.back()->opId == PrivCommand::http_request && (operations_.back()->opState & request_send_mask)) {
			return SendNextCommand();
		}
	}
	return res;
}

void CHttpControlSocket::SetSocketBufferSizes()
{
	if (!socket_) {
		return;
	}

	const int size_read = engine_.GetOptions().get_int(OPTION_SOCKET_BUFFERSIZE_RECV);
#if FZ_WINDOWS
	const int size_write = -1;
#else
	const int size_write = engine_.GetOptions().get_int(OPTION_SOCKET_BUFFERSIZE_SEND);
#endif
	socket_->set_buffer_sizes(size_read, size_write);
}
