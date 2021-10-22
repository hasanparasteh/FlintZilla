#ifndef FILEZILLA_ENGINE_HTTP_INTERNALCONNECT_HEADER
#define FILEZILLA_ENGINE_HTTP_INTERNALCONNECT_HEADER

#include "httpcontrolsocket.h"

#include <libfilezilla/tls_layer.hpp>

// Connect is special for HTTP: It is done on a per-command basis, so we need
// to establish a connection before each command.
// The general connect of the control socket is a NOOP.
class CHttpInternalConnectOpData final : public COpData, public CHttpOpData, public fz::event_handler
{
public:
	CHttpInternalConnectOpData(CHttpControlSocket & controlSocket, std::wstring const& host, unsigned short port, bool tls)
		: COpData(PrivCommand::http_connect, L"CHttpInternalConnectOpData")
		, CHttpOpData(controlSocket)
		, fz::event_handler(controlSocket.event_loop_)
		, host_(host)
		, port_(port)
		, tls_(tls)
	{}

	virtual ~CHttpInternalConnectOpData()
	{
		remove_handler();
	}

	virtual void operator()(fz::event_base const& ev) override
	{
		if (fz::dispatch<fz::certificate_verification_event>(ev, this, &CHttpInternalConnectOpData::OnVerifyCert)) {
			return;
		}
	}

	void OnVerifyCert(fz::tls_layer* source, fz::tls_session_info& info)
	{
		if (!controlSocket_.tls_layer_ || source != controlSocket_.tls_layer_.get()) {
			return;
		}

		controlSocket_.SendAsyncRequest(std::make_unique<CCertificateNotification>(std::move(info)));
	}

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }

	std::wstring host_;
	unsigned short port_;
	bool tls_;
};

#endif
