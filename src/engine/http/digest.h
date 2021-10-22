#ifndef FILEZILLA_ENGINE_HTTP_DIGEST_HEADER
#define FILEZILLA_ENGINE_HTTP_DIGEST_HEADER

#include "../../include/httpheaders.h"

namespace fz {
	class logger_interface;
	class uri;
}


typedef HttpHeaders HttpAuthParams;
typedef std::map<std::string, HttpAuthParams, fz::less_insensitive_ascii> HttpAuthChallenges;

HttpAuthChallenges ParseAuthChallenges(std::string const& header);

class Credentials;
std::string BuildDigestAuthorization(HttpAuthParams const& params, unsigned int & nonceCounter, std::string const& verb, fz::uri const& uri, std::string const& user, Credentials const& credentials, fz::logger_interface & logger);

#endif
