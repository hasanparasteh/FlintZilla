#ifndef FILEZILLA_ENGINE_SFTP_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_SFTP_FILETRANSFER_HEADER

#include "sftpcontrolsocket.h"

class CSftpFileTransferOpData final : public CFileTransferOpData, public CSftpOpData, public fz::event_handler
{
public:
	CSftpFileTransferOpData(CSftpControlSocket & controlSocket, CFileTransferCommand const& cmd)
		: CFileTransferOpData(L"CSftpFileTransferOpData", cmd)
		, CSftpOpData(controlSocket)
		, fz::event_handler(controlSocket.event_loop_)
	{}

	~CSftpFileTransferOpData();

	void OnSizeRequested();
	void OnOpenRequested(uint64_t offset);
	void OnNextBufferRequested(uint64_t processed);
	void OnFinalizeRequested(uint64_t lastWrite);

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int, COpData const&) override;

private:
	virtual void operator()(fz::event_base const& ev) override;
	void OnReaderEvent(reader_base*);
	void OnWriterEvent(writer_base*);

	std::unique_ptr<reader_base> reader_;
	std::unique_ptr<writer_base> writer_;
	bool finalizing_{};

	uint8_t const* base_address_{};
	fz::nonowning_buffer buffer_;
};

#endif
