#include "../filezilla.h"

#include "internalconnect.h"

int CHttpInternalConnectOpData::Send()
{
	if (!port_) {
		port_ = tls_ ? 443 : 80;
	}

	return controlSocket_.DoConnect(host_, port_);
}
