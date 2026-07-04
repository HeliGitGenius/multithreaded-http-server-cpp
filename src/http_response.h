#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <map>
#include <string>

// Builds a well-formed HTTP/1.1 response. The caller sets the status, headers
// and body, then calls serialize() to obtain the wire bytes. A HEAD response is
// produced by serialize(include_body = false), which keeps Content-Length but
// omits the body.
class HttpResponse {
public:
    explicit HttpResponse(int status = 200);

    void set_status(int status) { status_ = status; }
    void set_header(const std::string& key, const std::string& value) { headers_[key] = value; }
    void set_body(std::string body);

    int status() const { return status_; }

    // Serialize status line + headers + (optionally) body.
    std::string serialize(bool include_body = true) const;

    // Canonical reason phrase for a status code ("OK", "Not Found", ...).
    static std::string reason_phrase(int status);

    // Convenience: build a small HTML error page for a status code.
    static HttpResponse make_error(int status, bool keep_alive);

private:
    int status_;
    std::map<std::string, std::string> headers_;
    std::string body_;
};

// Guess a Content-Type from a file extension. Defaults to
// application/octet-stream.
std::string mime_type(const std::string& path);

#endif // HTTP_RESPONSE_H
