#include "http_response.h"

#include <ctime>
#include <sstream>

HttpResponse::HttpResponse(int status) : status_(status) {}

void HttpResponse::set_body(std::string body) {
    body_ = std::move(body);
    headers_["Content-Length"] = std::to_string(body_.size());
}

std::string HttpResponse::reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        default:  return "OK";
    }
}

namespace {
// RFC 7231 date, e.g. "Sat, 04 Jul 2026 15:04:05 GMT".
std::string http_date() {
    std::time_t t = std::time(nullptr);
    std::tm tm_buf{};
    gmtime_r(&t, &tm_buf);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_buf);
    return buf;
}
} // namespace

std::string HttpResponse::serialize(bool include_body) const {
    std::ostringstream os;
    os << "HTTP/1.1 " << status_ << ' ' << reason_phrase(status_) << "\r\n";
    os << "Date: " << http_date() << "\r\n";
    os << "Server: cpp-http-server/1.0\r\n";

    bool has_len = false;
    for (const auto& kv : headers_) {
        os << kv.first << ": " << kv.second << "\r\n";
        if (kv.first == "Content-Length") has_len = true;
    }
    if (!has_len) os << "Content-Length: " << body_.size() << "\r\n";

    os << "\r\n";
    if (include_body) os << body_;
    return os.str();
}

HttpResponse HttpResponse::make_error(int status, bool keep_alive) {
    HttpResponse resp(status);
    std::ostringstream body;
    body << "<!doctype html><html><head><title>" << status << ' '
         << reason_phrase(status) << "</title></head><body><h1>" << status << ' '
         << reason_phrase(status) << "</h1><hr><p>cpp-http-server/1.0</p></body></html>";
    resp.set_body(body.str());
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Connection", keep_alive ? "keep-alive" : "close");
    return resp;
}

std::string mime_type(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot + 1);

    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "txt") return "text/plain; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "pdf") return "application/pdf";
    return "application/octet-stream";
}
