#include "../filezilla.h"

#include "digest.h"

#include "../logging_private.h"

#include <libfilezilla/encode.hpp>
#include <libfilezilla/format.hpp>
#include <libfilezilla/hash.hpp>
#include <libfilezilla/uri.hpp>
#include <libfilezilla/util.hpp>

namespace {
void skipwscomma(char const*& p)
{
	while (*p && (*p == ' ' || *p == ',')) {
		++p;
	}
}

std::string unquote(char const* p, char const* end) {

	if (end - p > 2 && *p == '"') {
		std::string ret;

		++p;
		--end;

		ret.reserve(end - p);

		bool escaped = false;
		while (p != end) {
			if (escaped) {
				ret.push_back(*p);
				escaped = false;
			}
			else if (*p == '\\') {
				escaped = true;
			}
			else {
				ret.push_back(*p);
			}
			++p;
		}
		return ret;
	}
	else {
		return std::string(p, end);
	}
}


char const* getNext(char const*&p, char const*& sep)
{
	sep = 0;

	skipwscomma(p);

	if (!*p) {
		return nullptr;
	}

	char const* start = p;
	++p;

	while (*p) {
		if (*p == '=') {
			sep = p;
			++p;
			if (*p == '"') {
				// quoted-string part of auth-param
				++p;
				bool escaped = false;
				while (*p) {
					if (*p == '"') {
						if (!escaped) {
							++p;
							if (*p && *p != ',' && *p != ' ') {
								return nullptr;
							}
							return start;
						}
					}
					else {
						if (escaped) {
							escaped = false;
						}
						else if (*p == '\\') {
							escaped = true;
						}
					}
					++p;
				}

				return nullptr;
			}
			else {
				// token86 or token
				bool t86 = true;
				while (*p && *p != ',' && *p != ' ') {
					if (*p != '=') {
						t86 = false;
					}
					++p;
				}
				if (t86) {
					sep = nullptr;
				}
				return start;
			}
		}
		else if (*p == ' ' || *p == ',') {
			// token86, or next scheme. Caller decides
			return start;
		}
		++p;
	}

	// token86, or next scheme. Caller decides
	return start;
}
}

HttpAuthChallenges ParseAuthChallenges(std::string const& header)
{
	// See RFC 7235 how to parse this terrible header.

	HttpAuthChallenges ret;

	char const* p = header.c_str();
	char const* next_scheme = 0;
	while (*p) {

		char const* scheme_start;
		if (next_scheme) {
			scheme_start = next_scheme;
			next_scheme = 0;
		}
		else {
			// Extract the scheme
			skipwscomma(p);
			scheme_start = p;
			while (*p && *p != ' ') {
				++p;
			}

			if (!*p || scheme_start == p) {
				return ret;
			}
		}
		auto const scheme_end = p;

		// Now extract auth params

		HttpAuthParams params;

		while (*p) {
			char const* sep = 0;
			auto start = getNext(p, sep);
			if (!start) {
				break;
			}

			if (!sep) {
				if (params.empty()) {
					// token86
					params[""] = std::string(start, p);
				}
				else {
					// It's the next scheme
					next_scheme = start;
				}
				break;
			}
			else {
				params[std::string(start, sep)] = unquote(sep + 1, p);
			}
		}

		if (params.empty()) {
			return ret;
		}

		ret[std::string(scheme_start, scheme_end)] = params;
	}

	return ret;
}

namespace {
std::string quote(std::string const& in)
{
	return "\"" + fz::replaced_substrings(fz::replaced_substrings(in, "\\", "\\\\"), "\"", "\\\"") + "\"";
}

template<typename T, typename K>
typename T::mapped_type get(T const& t, K && key)
{
	auto it = t.find(std::forward<K>(key));
	if (it != t.cend()) {
		return it->second;
	}
	return typename T::mapped_type();
}
}

std::string BuildDigestAuthorization(HttpAuthParams const& params, unsigned int & nonceCounter, std::string const& verb, fz::uri const& uri, std::string const& user, Credentials const& credentials, fz::logger_interface & logger)
{
	// See RFC 7616

	std::string auth = "Digest username=";

	auth += quote(user);

	std::string const opaque = get(params, "opaque");
	std::string const nonce = get(params, "nonce");
	std::string const realm = get(params, "realm");

	auth += ", realm=" + quote(realm);
	auth += ", nonce=" + quote(nonce);

	if (!opaque.empty()) {
		auth += ", opaque=" + quote(opaque);
	}
	auth += ", uri=" + quote(uri.to_string());

	std::string fullAlgorithm = get(params, "algorithm");
	if (fullAlgorithm.empty()) {
		fullAlgorithm = "MD5";
	}
	auth += ", algorithm=" + fullAlgorithm;

	unsigned int nc = nonceCounter++;
	auth += ", nc=" + fz::sprintf("%x", nc);


	bool sess = false;
	auto algo = fz::str_toupper_ascii(fullAlgorithm);
	if (algo.size() > 5 && algo.substr(algo.size() - 5) == "-sess") {
		sess = true;
		algo = algo.substr(0, algo.size() - 5);
	}

	std::vector<uint8_t> (*h)(std::string_view const&) = 0;
	if (algo == "MD5") {
		h = &fz::md5;
	}
	else if (algo == "SHA-256") {
		h = &fz::sha256;
	}
	else {
		logger.log(logmsg::error, _("Server requested unsupported digest authentication algorithm: %s"), fullAlgorithm);
		return std::string();
	}

	bool qop = false;
	auto qops = fz::strtok(get(params, "qop"), ",");
	if (!qops.empty()) {
		if (std::find(qops.cbegin(), qops.cend(), "auth") == qops.cend()) {
			logger.log(logmsg::error, _("Server requested unsupported quality-of-protection: %s"), get(params, "qop"));
			return std::string();
		}
		qop = true;
	}

	auto bytes = fz::random_bytes(16);
	std::string const cnonce = fz::base64_encode(std::string(bytes.cbegin(), bytes.cend()));
	auth += ", cnonce=" + quote(cnonce);

	std::string a1 = fz::hex_encode<std::string>(h(user + ":" + realm + ":" + fz::to_utf8(credentials.GetPass())));
	std::string ha2 = fz::hex_encode<std::string>(h(verb + ":" + uri.to_string()));

	std::string response;
	if (sess) {
		a1 = fz::hex_encode<std::string>(h(a1 + ":" + nonce + ":" + cnonce));
	}

	if (qop) {
		auth += ", qop=auth";
		response = fz::hex_encode<std::string>(h(a1 + ":" + nonce + ":" + fz::sprintf("%x", nc) + ":" + cnonce + ":auth:" + ha2));
	}
	else {
		response = fz::hex_encode<std::string>(h(a1 + ":" + nonce + ":" + ha2));
	}

	auth += ", response=" + quote(response);

	return auth;
}
