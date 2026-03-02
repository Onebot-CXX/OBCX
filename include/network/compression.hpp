#pragma once

#include <boost/beast/http.hpp>

namespace obcx::network {

namespace http = boost::beast::http;

void decompress_inplace(http::response<http::string_body> &res);

} // namespace obcx::network
