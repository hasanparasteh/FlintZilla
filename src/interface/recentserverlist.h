#ifndef FILEZILLA_INTERFACE_RECENTSERVERLIST_HEADER
#define FILEZILLA_INTERFACE_RECENTSERVERLIST_HEADER

#include "xmlfunctions.h"

#include <deque>

class CRecentServerList
{
public:
	static void SetMostRecentServer(Site const& site);
	static void SetMostRecentServers(std::deque<Site> const& sites, bool lockMutex = true);
	static const std::deque<Site> GetMostRecentServers(bool lockMutex = true);
	static void Clear();
};

#endif
