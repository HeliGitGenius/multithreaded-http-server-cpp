#include "http_request.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Percent-decode a URL path. Invalid escapes are left as-is.
std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            std::string hex = s.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

} // namespace

std::string HttpRequest::header(const std::string& key) const {
    auto it = headers.find(to_lower(key));
    return it == headers.end() ? std::string() : it->second;
}

ParseStatus parse_request(const std::string& raw, HttpRequest* out, size_t* consumed) {
    const std::string delim = "\r\n\r\n";
    size_t header_end = raw.find(delim);
    if (header_end == std::string::npos) return ParseStatus::Incomplete;

    HttpRequest req;

    // --- Request line ---
    size_t line_end = raw.find("\r\n");
    std::string line = raw.substr(0, line_end);
    {
        size_t sp1 = line.find(' ');
        if (sp1 == std::string::npos) return ParseStatus::BadRequest;
        size_t sp2 = line.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) return ParseStatus::BadRequest;

        req.method = line.substr(0, sp1);
        req.target = line.substr(sp1 + 1, sp2 - sp1 - 1);
        req.version = line.substr(sp2 + 1);

        if (req.method.empty() || req.target.empty()) return ParseStatus::BadRequest;
        if (req.version != "HTTP/1.0" && req.version != "HTTP/1.1") return ParseStatus::BadRequest;

        // Strip query string, then percent-decode.
        std::string path_part = req.target;
        size_t q = path_part.find('?');
        if (q != std::string::npos) path_part = path_part.substr(0, q);
        req.path = url_decode(path_part);
    }

    // --- Header lines ---
    size_t pos = line_end + 2;
    while (pos < header_end) {
        size_t eol = raw.find("\r\n", pos);
        if (eol == std::string::npos || eol > header_end) eol = header_end;
        std::string h = raw.substr(pos, eol - pos);
        pos = eol + 2;
        if (h.empty()) break;

        size_t colon = h.find(':');
        if (colon == std::string::npos) return ParseStatus::BadRequest;
        std::string name = to_lower(trim(h.substr(0, colon)));
        std::string value = trim(h.substr(colon + 1));
        if (name.empty()) return ParseStatus::BadRequest;
        req.headers[name] = value;
    }

    // --- Body (only if Content-Length present) ---
    size_t body_start = header_end + delim.size();
    size_t content_length = 0;
    auto cl = req.headers.find("content-length");
    if (cl != req.headers.end()) {
        char* end = nullptr;
        long v = std::strtol(cl->second.c_str(), &end, 10);
        if (end == nullptr || *end != '\0' || v < 0) return ParseStatus::BadRequest;
        content_length = static_cast<size_t>(v);
    }

    if (raw.size() < body_start + content_length) return ParseStatus::Incomplete;
    req.body = raw.substr(body_start, content_length);

    // --- Keep-alive resolution ---
    std::string conn = to_lower(req.header("connection"));
    if (req.version == "HTTP/1.1") {
        req.keep_alive = (conn != "close");
    } else {
        req.keep_alive = (conn == "keep-alive");
    }

    if (consumed) *consumed = body_start + content_length;
    *out = std::move(req);
    return ParseStatus::Ok;
}
