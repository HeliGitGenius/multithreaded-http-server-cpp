#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <map>
#include <string>

// A parsed HTTP/1.1 request. Only the parts this server needs are kept.
struct HttpRequest {
    std::string method;   // "GET", "HEAD", "POST", ...
    std::string target;   // raw request target, e.g. "/index.html?x=1"
    std::string path;     // decoded path portion, e.g. "/index.html"
    std::string version;  // "HTTP/1.1"
    std::map<std::string, std::string> headers; // lower-cased keys
    std::string body;

    bool keep_alive = false; // resolved from version + Connection header

    // Look up a header (case-insensitive). Returns empty string if absent.
    std::string header(const std::string& key) const;
};

// Result of a parse attempt.
enum class ParseStatus {
    Ok,
    Incomplete,  // headers not fully received yet
    BadRequest,  // malformed request line / headers -> 400
};

// Parse a raw buffer. If Ok, *out is filled and *consumed is set to the number
// of bytes forming the request (headers + body). Never throws.
ParseStatus parse_request(const std::string& raw, HttpRequest* out, size_t* consumed);

#endif // HTTP_REQUEST_H
