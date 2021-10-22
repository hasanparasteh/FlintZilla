#include "filezilla.h"

#include "../include/externalipresolver.h"
#include "../include/misc.h"

#include <libfilezilla/format.hpp>
#include <libfilezilla/iputils.hpp>

#include <regex>

namespace {
fz::mutex s_sync;
std::string ip;
bool checked = false;
}

CExternalIPResolver::CExternalIPResolver(fz::thread_pool & pool, fz::event_handler & handler)
	: fz::event_handler(handler.event_loop_)
	, thread_pool_(pool)
	, m_handler(&handler)
{
}

CExternalIPResolver::~CExternalIPResolver()
{
	remove_handler();
}

void CExternalIPResolver::GetExternalIP(std::wstring const& address, fz::address_type protocol, bool force)
{
	{
		fz::scoped_lock l(s_sync);
		if (checked) {
			if (force) {
				checked = false;
			}
			else {
				m_done = true;
				return;
			}
		}
	}

	m_address = address;
	m_protocol = protocol;

	std::wstring host;
	size_t pos = address.find(L"://");
	if (pos != std::wstring::npos) {
		host = address.substr(pos + 3);
	}
	else {
		host = address;
	}

	pos = host.find('/');
	if (pos != std::wstring::npos) {
		host = host.substr(0, pos);
	}

	std::wstring hostWithPort = host;
	pos = host.rfind(':');
	if (pos != std::wstring::npos) {
		std::wstring port = host.substr(pos + 1);
		m_port = fz::to_integral<decltype(m_port)>(port);
		if (m_port < 1 || m_port > 65535) {
			m_port = 80;
		}
		host = host.substr(0, pos);
	}
	else {
		m_port = 80;
	}

	if (host.empty()) {
		m_done = true;
		return;
	}

	socket_ = std::make_unique<fz::socket>(thread_pool_, this);

	int res = socket_->connect(fz::to_native(host), m_port, protocol);
	if (res) {
		Close(false);
		return;
	}

	m_sendBuffer = fz::sprintf("GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: close\r\n\r\n", fz::to_utf8(address), fz::to_utf8(hostWithPort), fz::replaced_substrings(PACKAGE_STRING, " ", "/"));
}

void CExternalIPResolver::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::socket_event>(ev, this, &CExternalIPResolver::OnSocketEvent);
}

void CExternalIPResolver::OnSocketEvent(fz::socket_event_source*, fz::socket_event_flag t, int error)
{
	if (!socket_) {
		return;
	}

	if (error) {
		Close(false);
	}

	switch (t)
	{
	case fz::socket_event_flag::read:
		OnReceive();
		break;
	case fz::socket_event_flag::connection:
		OnConnect(error);
		break;
	case fz::socket_event_flag::write:
		OnSend();
		break;
	default:
		break;
	}

}

void CExternalIPResolver::OnConnect(int error)
{
	if (error) {
		Close(false);
	}
	else {
		OnSend();
	}
}

void CExternalIPResolver::OnReceive()
{
	if (!m_sendBuffer.empty()) {
		return;
	}

	while (socket_) {
		int error;
		int read = socket_->read(recvBuffer_.get(4096), 4096, error);
		if (read == -1) {
			if (error != EAGAIN) {
				Close(false);
			}
			return;
		}
		else if (!read) {
			if (m_transferEncoding != chunked && !m_data.empty()) {
				OnData(nullptr, 0);
			}
			else {
				Close(false);
			}
			return;
		}

		recvBuffer_.add(read);

		if (!m_gotHeader) {
			OnHeader();
		}
		else {
			if (m_transferEncoding == chunked) {
				OnChunkedData();
			}
			else {
				OnData(recvBuffer_.get(), recvBuffer_.size());
				recvBuffer_.clear();
			}
		}
	}
}

void CExternalIPResolver::OnSend()
{
	while (!m_sendBuffer.empty()) {
		int error;
		int written = socket_->write(m_sendBuffer.c_str(), static_cast<int>(m_sendBuffer.size()), error);
		if (written == -1) {
			if (error != EAGAIN) {
				Close(false);
			}
			return;
		}

		if (!written) {
			Close(false);
			return;
		}

		m_sendBuffer = m_sendBuffer.substr(written);
		if (m_sendBuffer.empty()) {
			OnReceive();
		}
	}
}

void CExternalIPResolver::Close(bool successful)
{
	m_sendBuffer.clear();
	recvBuffer_.clear();
	socket_.reset();

	if (m_done) {
		return;
	}

	m_done = true;

	{
		fz::scoped_lock l(s_sync);
		if (!successful) {
			ip.clear();
		}
		checked = true;
	}

	if (m_handler) {
		m_handler->send_event<CExternalIPResolveEvent>();
		m_handler = nullptr;
	}
}

void CExternalIPResolver::OnHeader()
{
	// Parse the HTTP header.
	// We do just the neccessary parsing and silently ignore most header fields
	// Redirects are supported though if the server sends the Location field.

	while (!recvBuffer_.empty()) {
		// Find line ending
		size_t i = 0;
		for (i = 0; (i + 1) < recvBuffer_.size(); ++i) {
			if (recvBuffer_[i] == '\r') {
				if (recvBuffer_[i + 1] != '\n') {
					Close(false);
					return;
				}
				break;
			}
		}
		if ((i + 1) >= recvBuffer_.size()) {
			if (recvBuffer_.size() >= 4096) {
				// We don't support header lines larger than 4096
				Close(false);
			}
			return;
		}

		std::string const line(recvBuffer_.get(), recvBuffer_.get() + i);
		recvBuffer_.consume(i + 2);

		if (!m_responseCode) {
			if (line.size() < 13 || !fz::equal_insensitive_ascii(line.substr(0, 7), std::string("HTTP/1."))) {
				// Invalid HTTP Status-Line
				Close(false);
				return;
			}

			if (line[9]  < '1' || line[9]  > '5' ||
				line[10] < '0' || line[10] > '9' ||
				line[11] < '0' || line[11] > '9')
			{
				// Invalid response code
				Close(false);
				return;
			}

			m_responseCode = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';

			if (m_responseCode >= 400) {
				// Failed request
				Close(false);
				return;
			}

			if (m_responseCode == 305) {
				// Unsupported redirect
				Close(false);
				return;
			}
		}
		else {
			if (!i) {
				// End of header, data from now on

				// Redirect if neccessary
				if (m_responseCode >= 300) {
					std::wstring const location = m_location;
					if (location.empty()) {
						Close(false);
					}
					else {
						socket_.reset();
						ResetHttpData();
						GetExternalIP(location, m_protocol);
					}
					return;
				}

				m_gotHeader = true;

				if (!recvBuffer_.empty()) {
					if (m_transferEncoding == chunked) {
						OnChunkedData();
					}
					else {
						OnData(recvBuffer_.get(), recvBuffer_.size());
						recvBuffer_.clear();
					}
				}
				return;
			}
			if (line.size() > 10 && fz::equal_insensitive_ascii(line.substr(0, 10), std::string("Location: "))) {
				m_location = fz::to_wstring_from_utf8(line.substr(10));
			}
			else if (line.size() > 19 && fz::equal_insensitive_ascii(line.substr(0, 19), std::string("Transfer-Encoding: "))) {
				std::string const encoding = line.substr(19);
				if (fz::equal_insensitive_ascii(encoding, std::string("chunked"))) {
					m_transferEncoding = chunked;
				}
				else if (fz::equal_insensitive_ascii(encoding, std::string("identity"))) {
					m_transferEncoding = identity;
				}
				else {
					m_transferEncoding = unknown;
				}
			}
		}
	}
}

void CExternalIPResolver::OnData(unsigned char* buffer, size_t len)
{
	if (buffer) {
		size_t i;
		for (i = 0; i < len; ++i) {
			auto const& c = buffer[i];
			if (c == '\r' || c == '\n') {
				break;
			}
			if (c < 0x20 || c & 0x80) {
				Close(false);
				return;
			}
		}

		if (i) {
			m_data.append(buffer, buffer + i);
		}

		if (i == len) {
			if (len >= 4096) {
				// Too long line
				Close(false);
			}
			return;
		}
	}

	if (m_protocol == fz::address_type::ipv6) {
		if (!m_data.empty() && m_data[0] == '[') {
			if (m_data.back() != ']') {
				Close(false);
				return;
			}
			m_data = m_data.substr(1, m_data.size() - 2);
		}

		if (fz::get_ipv6_long_form(m_data).empty()) {
			Close(false);
			return;
		}

		fz::scoped_lock l(s_sync);
		ip = m_data;
	}
	else {
		// Validate ip address
		std::string const digit = "0*[0-9]{1,3}";
		char const* const dot = "\\.";
		std::string const exp = "(^|[^\\.[:digit:]])(" + digit + dot + digit + dot + digit + dot + digit + ")([^\\.[:digit:]]|$)";

		std::regex const regex(exp);
		std::smatch m;
		if (!std::regex_search(m_data, m, regex)) {
			Close(false);
			return;
		}

		fz::scoped_lock l(s_sync);
		ip = m[2];
	}

	Close(true);
}

void CExternalIPResolver::ResetHttpData()
{
	recvBuffer_.clear();
	m_sendBuffer.clear();
	m_gotHeader = false;
	m_location.clear();
	m_responseCode = 0;

	m_transferEncoding = unknown;

	m_chunkData = t_chunkData();
}

void CExternalIPResolver::OnChunkedData()
{
	while (!recvBuffer_.empty()) {
		if (m_chunkData.size != 0) {
			size_t dataLen = recvBuffer_.size();
			if (m_chunkData.size < dataLen) {
				dataLen = static_cast<size_t>(m_chunkData.size);
			}
			OnData(recvBuffer_.get(), dataLen);
			if (recvBuffer_.empty()) {
				return;
			}

			recvBuffer_.consume(dataLen);
			m_chunkData.size -= dataLen;

			if (!m_chunkData.size) {
				m_chunkData.terminateChunk = true;
			}
		}

		// Find line ending
		size_t i = 0;
		for (i = 0; (i + 1) < recvBuffer_.size(); ++i) {
			if (recvBuffer_[i] == '\r') {
				if (recvBuffer_[i + 1] != '\n') {
					Close(false);
					return;
				}
				break;
			}
		}
		if ((i + 1) >= recvBuffer_.size()) {
			if (recvBuffer_.size() >= 4096) {
				// We don't support lines larger than 4096
				Close(false);
			}
			return;
		}

		// Here we have a line.
		if (m_chunkData.terminateChunk) {
			if (i) {
				// Chunk has to end with CRLF
				Close(false);
				return;
			}
			m_chunkData.terminateChunk = false;
		}
		else if (m_chunkData.getTrailer) {
			if (!i) {
				if (m_data.empty()) {
					Close(false);
				}
				else {
					OnData(nullptr, 0);
				}
				return;
			}

			// Ignore the trailer
		}
		else {
			// Read chunk size

			for (size_t j = 0; j < i; ++j) {
				unsigned char const& c = recvBuffer_[j];
				if (c >= '0' && c <= '9') {
					m_chunkData.size *= 16;
					m_chunkData.size += c - '0';
				}
				else if (c >= 'A' && c <= 'F') {
					m_chunkData.size *= 16;
					m_chunkData.size += c - 'A' + 10;
				}
				else if (c >= 'a' && c <= 'f') {
					m_chunkData.size *= 16;
					m_chunkData.size += c - 'a' + 10;
				}
				else if (c == ';' || c == ' ') {
					break;
				}
				else {
					// Invalid size
					Close(false);
					return;
				}
			}
			if (m_chunkData.size == 0) {
				m_chunkData.getTrailer = true;
			}
		}

		recvBuffer_.consume(i + 2);
	}
}

bool CExternalIPResolver::Successful() const
{
	fz::scoped_lock l(s_sync);
	return !ip.empty();
}

std::string CExternalIPResolver::GetIP() const
{
	fz::scoped_lock l(s_sync);
	return ip;
}
