#ifndef FILEZILLA_ENGINE_FTP_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_FTP_FILETRANSFER_HEADER

#include "ftpcontrolsocket.h"

enum filetransferStates
{
	filetransfer_init = 0,
	filetransfer_waitcwd,
	filetransfer_waitlist,
	filetransfer_size,
	filetransfer_mdtm,
	filetransfer_resumetest,
	filetransfer_transfer,
	filetransfer_waittransfer,
	filetransfer_waitresumetest,
	filetransfer_mfmt
};

class CFtpFileTransferOpData final : public CFileTransferOpData, public CFtpTransferOpData, public CFtpOpData
{
public:
	CFtpFileTransferOpData(CFtpControlSocket& controlSocket, CFileTransferCommand const& cmd);

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const&) override;

	int TestResumeCapability();

	bool fileDidExist_{true};
};

#endif
