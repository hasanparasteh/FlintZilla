#ifndef FILEZILLA_INTERFACE_AUTO_ASCII_FILES_HEADER
#define FILEZILLA_INTERFACE_AUTO_ASCII_FILES_HEADER

class CAutoAsciiFiles final
{
public:
	static bool TransferLocalAsAscii(std::wstring const& local_file, ServerType server_type);
	static bool TransferRemoteAsAscii(std::wstring const& remote_file, ServerType server_type);

	static void SettingsChanged();
protected:
	static bool IsAsciiExtension(std::wstring const& ext);
	static std::vector<std::wstring> ascii_extensions_;
};

#endif
