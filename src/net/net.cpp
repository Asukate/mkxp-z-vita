//
//  net.cpp — Vita stub (network disabled for offline game)
//  mkxp-z
//
//  Created by ゾロアーク on 12/29/20.
//

#include "net.h"
#include "util/exception.h"

namespace mkxp_net {

/* Stub — no networking on Vita */

HTTPResponse::HTTPResponse() : _status(0) {}
HTTPResponse::~HTTPResponse() {}
int HTTPResponse::status() { return _status; }
std::string &HTTPResponse::body() { return _body; }
StringMap &HTTPResponse::headers() { return _headers; }

HTTPRequest::HTTPRequest(const char *dest, bool follow_redirects)
    : destination(dest), follow_location(follow_redirects)
{}
HTTPRequest::~HTTPRequest() {}
StringMap &HTTPRequest::headers() { return _headers; }

HTTPResponse HTTPRequest::get()
{
    throw Exception(Exception::MKXPError,
        "Networking is not supported on this platform");
}

HTTPResponse HTTPRequest::post(StringMap &postData)
{
    (void)postData;
    throw Exception(Exception::MKXPError,
        "Networking is not supported on this platform");
}

HTTPResponse HTTPRequest::post(const char *body, const char *content_type)
{
    (void)body;
    (void)content_type;
    throw Exception(Exception::MKXPError,
        "Networking is not supported on this platform");
}

}
