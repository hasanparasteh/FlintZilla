#ifndef FILEZILLA_COMMONUI_SITEMANAGER_HEADER
#define FILEZILLA_COMMONUI_SITEMANAGER_HEADER

#include "xml_file.h"
#include "visibility.h"

class FZCUI_PUBLIC_SYMBOL CSiteManagerXmlHandler
{
public:
	virtual ~CSiteManagerXmlHandler() = default;

	// Adds a folder and descents
	virtual bool AddFolder(std::wstring const& name, bool expanded) = 0;
	virtual bool AddSite(std::unique_ptr<Site> data) = 0;

	// Go up a level
	virtual bool LevelUp() { return true; } // *Ding*
};

class app_paths;

// for now just read-only interface and rest still in interface CSiteManager waiting for refactoring
class FZCUI_PUBLIC_SYMBOL site_manager
{
public:
	virtual ~site_manager() = default;

	static std::pair<std::unique_ptr<Site>, Bookmark> GetSiteByPath(app_paths const& paths, std::wstring sitePath, std::wstring& error);

	static bool UnescapeSitePath(std::wstring path, std::vector<std::wstring>& result);
	static std::wstring EscapeSegment(std::wstring segment);

	static void UpdateOneDrivePath(CServerPath & bookmark);
	static void UpdateGoogleDrivePath(CServerPath & bookmark);

	static site_colour GetColourFromIndex(int i);
protected:
	static bool ReadBookmarkElement(Bookmark & bookmark, pugi::xml_node element);

	static bool Load(std::wstring const& settings_file, CSiteManagerXmlHandler& pHandler, std::wstring& error);
	static bool Load(pugi::xml_node element, CSiteManagerXmlHandler& pHandler);
	static std::unique_ptr<Site> ReadServerElement(pugi::xml_node element);

	static pugi::xml_node GetElementByPath(pugi::xml_node node, std::vector<std::wstring> const& segments);
	static std::wstring BuildPath(wchar_t root, std::vector<std::wstring> const& segments);

	static bool LoadPredefined(CLocalPath const& defaults_dir, CSiteManagerXmlHandler& handler);
};

#endif
