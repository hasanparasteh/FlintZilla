#ifndef FILEZILLA_ENGINE_SFTP_SFTPCONTROLSOCKET_HEADER
#define FILEZILLA_ENGINE_SFTP_SFTPCONTROLSOCKET_HEADER

#include "../controlsocket.h"

#include <libfilezilla/rate_limiter.hpp>

namespace fz {
class process;
}

class CSftpInputThread;
struct sftp_message;
struct sftp_list_message;

class CSftpControlSocket final : public CControlSocket, public fz::bucket
{
public:
	CSftpControlSocket(CFileZillaEnginePrivate & engine);
	virtual ~CSftpControlSocket();

	virtual void Connect(CServer const& server, Credentials const& credentials) override;
	virtual void List(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), int flags = 0) override;
	void ChangeDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring(), bool link_discovery = false);
	virtual void FileTransfer(CFileTransferCommand const& cmd) override;
	virtual void Delete(CServerPath const& path, std::vector<std::wstring>&& files) override;
	virtual void RemoveDir(CServerPath const& path = CServerPath(), std::wstring const& subDir = std::wstring()) override;
	virtual void Mkdir(CServerPath const& path) override;
	virtual void Rename(CRenameCommand const& command) override;
	virtual void Chmod(CChmodCommand const& command) override;
	virtual void Cancel() override;

	virtual bool SetAsyncRequestReply(CAsyncRequestNotification *pNotification) override;

protected:
	virtual void Push(std::unique_ptr<COpData> && pNewOpData) override;

	// Replaces filename"with"quotes with
	// "filename""with""quotes"
	std::wstring QuoteFilename(std::wstring const& filename);

	virtual int DoClose(int nErrorCode = FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR) override;

	void ProcessReply(int result, std::wstring const& reply);

	int SendCommand(std::wstring const& cmd, std::wstring const& show = std::wstring());
	int AddToStream(std::wstring const& cmd);
	int AddToStream(std::string const& cmd);

	virtual void wakeup(fz::direction::type const d) override;
	void OnQuotaRequest(fz::direction::type const d);

#ifndef FZ_WINDOWS
	int shm_fd_{-1};
#endif
	std::unique_ptr<fz::process> process_;
	std::unique_ptr<CSftpInputThread> input_thread_;

	virtual void operator()(fz::event_base const& ev) override;
	void OnSftpEvent(sftp_message const& message);
	void OnSftpListEvent(sftp_list_message const& message);
	void OnTerminate(std::wstring const& error);

	std::wstring m_requestPreamble;
	std::wstring m_requestInstruction;

	CSftpEncryptionNotification m_sftpEncryptionDetails;

	int result_{};
	std::wstring response_;

	friend class CProtocolOpData<CSftpControlSocket>;
	friend class CSftpChangeDirOpData;
	friend class CSftpChmodOpData;
	friend class CSftpConnectOpData;
	friend class CSftpDeleteOpData;
	friend class CSftpFileTransferOpData;
	friend class CSftpListOpData;
	friend class CSftpMkdirOpData;
	friend class CSftpRemoveDirOpData;
	friend class CSftpRenameOpData;
};

typedef CProtocolOpData<CSftpControlSocket> CSftpOpData;

#endif
