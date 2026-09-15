#include "network/http_client.hpp"

#include "common/logger.hpp"
#include "curl_asio_multi.hpp"
#include "http_client_impl.hpp"

#include <boost/asio/this_coro.hpp>
#include <boost/system/system_error.hpp>
#include <cctype>
#include <limits>
#include <string>
#include <utility>

namespace obcx::network {
namespace {

constexpr std::uint64_t kMaximumResponseHeaderBytes = 64ULL * 1024ULL;

[[nodiscard]] auto equal_header_name(std::string_view left,
                                     std::string_view right) -> bool {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(left[index])) !=
        std::tolower(static_cast<unsigned char>(right[index]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] auto has_header(const std::map<std::string, std::string> &headers,
                              std::string_view name) -> bool {
  return std::ranges::any_of(headers, [name](const auto &entry) {
    return equal_header_name(entry.first, name);
  });
}

[[nodiscard]] auto build_headers(
    const common::ConnectionConfig &config, std::string_view body,
    const std::map<std::string, std::string> &supplied)
    -> std::map<std::string, std::string> {
  std::map<std::string, std::string> headers;
  const auto set_default = [&headers, &supplied](std::string name,
                                                 std::string value) {
    if (!has_header(supplied, name)) {
      headers.emplace(std::move(name), std::move(value));
    }
  };
  set_default("User-Agent",
              "Mozilla/5.0 (X11; Linux x86_64; rv:142.0) Gecko/20100101 "
              "Firefox/142.0");
  set_default("Accept",
              "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q="
              "0.8");
  set_default("Accept-Language", "en-US,en;q=0.5");
  set_default("DNT", "1");
  set_default("Sec-GPC", "1");
  set_default("Connection", "keep-alive");
  set_default("Upgrade-Insecure-Requests", "1");
  set_default("Sec-Fetch-Dest", "document");
  set_default("Sec-Fetch-Mode", "navigate");
  set_default("Sec-Fetch-Site", "cross-site");
  set_default("Priority", "u=0, i");
  set_default("Pragma", "no-cache");
  set_default("Cache-Control", "no-cache");
  if (!body.empty()) {
    set_default("Content-Type", "application/json");
  }
  if (!config.access_token.empty() && !has_header(supplied, "Authorization")) {
    headers.emplace("Authorization", "Bot " + config.access_token);
  }
  for (const auto &[name, value] : supplied) {
    headers.insert_or_assign(name, value);
  }
  return headers;
}

[[nodiscard]] auto request_url(const common::ConnectionConfig &config,
                               std::string_view path) -> std::string {
  std::string host = config.host;
  if (host.find(':') != std::string::npos &&
      !(host.starts_with('[') && host.ends_with(']'))) {
    host = '[' + host + ']';
  }
  const auto scheme = config.port == 443 || config.use_ssl ? "https" : "http";
  std::string normalized_path;
  if (path.empty()) {
    normalized_path = "/";
  } else if (path.front() == '/') {
    normalized_path = path;
  } else {
    normalized_path.reserve(path.size() + 1);
    normalized_path.push_back('/');
    normalized_path.append(path);
  }
  return std::string{scheme} + "://" + host + ':' +
         std::to_string(config.port) + normalized_path;
}

[[nodiscard]] auto curl_request(
    const common::ConnectionConfig &config,
    const std::optional<detail::CurlProxySettings> &proxy,
    const detail::CurlHttpMethod method, std::string_view path,
    std::string_view body, const std::map<std::string, std::string> &headers,
    const std::uint64_t response_body_limit) -> detail::CurlRequest {
  return {
      .method = method,
      .url = request_url(config, path),
      .headers = build_headers(config, body, headers),
      .body = std::string{body},
      .connect_timeout = config.connect_timeout,
      .total_timeout = config.connect_timeout,
      .maximum_response_bytes = response_body_limit,
      .maximum_header_bytes = kMaximumResponseHeaderBytes,
      .ca_bundle = std::nullopt,
      .proxy = proxy,
  };
}

[[nodiscard]] auto strip_line_end(std::string line) -> std::string {
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.pop_back();
  }
  return line;
}

[[nodiscard]] auto make_response(detail::CurlResponse value) -> HttpResponse {
  HttpResponse response;
  if (value.status_code > std::numeric_limits<unsigned int>::max()) {
    throw HttpClientError("HTTP response status is out of range");
  }
  response.status_code = static_cast<unsigned int>(value.status_code);
  response.body = std::move(value.body);
  response.raw_response.version(11);
  response.raw_response.result(response.status_code);
  response.raw_response.body() = response.body;
  for (auto &header_line : value.header_lines) {
    auto line = strip_line_end(std::move(header_line));
    if (line.empty() || line.starts_with("HTTP/")) {
      continue;
    }
    const auto separator = line.find(':');
    if (separator == std::string::npos) {
      continue;
    }
    auto name = line.substr(0, separator);
    auto value_start = separator + 1;
    while (value_start < line.size() &&
           std::isspace(static_cast<unsigned char>(line[value_start]))) {
      ++value_start;
    }
    if (equal_header_name(name, "Content-Encoding") ||
        equal_header_name(name, "Content-Length")) {
      continue;
    }
    try {
      response.raw_response.set(name, line.substr(value_start));
    } catch (const std::exception &) {
      // libcurl accepted the wire header; omit values Beast cannot represent.
    }
  }
  response.raw_response.prepare_payload();
  return response;
}

[[noreturn]] void throw_request_error(
    std::string_view method, const boost::system::error_code &error,
    const HttpRequestSubmissionState submission_state) {
  std::string message{"HTTP "};
  message.append(method);
  if (error == asio::error::timed_out) {
    message.append(" request timed out");
  } else if (error == asio::error::operation_aborted) {
    message.append(" request cancelled");
  } else if (error == asio::error::message_size) {
    message.append(" response body limit exceeded");
  } else {
    message.append(" request failed");
  }
  throw HttpClientError(message, submission_state);
}

} // namespace

HttpClient::HttpClient(asio::io_context &ioc,
                       const common::ConnectionConfig &config)
    : HttpClient(ioc.get_executor(), config) {}

HttpClient::HttpClient(asio::any_io_executor executor,
                       const common::ConnectionConfig &config)
    : pimpl_(std::make_unique<Impl>(config)) {
  (void)executor;
}

HttpClient::~HttpClient() = default;

auto HttpClient::post(std::string_view path, std::string_view body,
                      const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  auto executor = co_await asio::this_coro::executor;
  auto driver = pimpl_->driver_for(executor);
  auto value =
      curl_request(pimpl_->config, pimpl_->proxy, detail::CurlHttpMethod::Post,
                   path, body, headers, pimpl_->response_body_limit);
  try {
    auto response = co_await driver->perform(std::move(value));
    pimpl_->connected = true;
    co_return make_response(std::move(response));
  } catch (const boost::system::system_error &error) {
    pimpl_->connected = false;
    OBCX_WARN("HTTP POST request failed");
    const auto submission_state =
        detail::curl_error_definitely_not_submitted(error.code())
            ? HttpRequestSubmissionState::DefinitelyNotSubmitted
            : HttpRequestSubmissionState::PossiblySubmitted;
    throw_request_error("POST", error.code(), submission_state);
  } catch (const HttpClientError &) {
    pimpl_->connected = false;
    throw;
  } catch (const std::exception &) {
    pimpl_->connected = false;
    OBCX_WARN("HTTP POST request could not be prepared");
    throw HttpClientError("HTTP POST request could not be prepared",
                          HttpRequestSubmissionState::DefinitelyNotSubmitted);
  }
}

auto HttpClient::get(std::string_view path,
                     const std::map<std::string, std::string> &headers,
                     const std::optional<std::uint64_t> response_body_limit)
    -> asio::awaitable<HttpResponse> {
  const auto selected_limit =
      response_body_limit.value_or(pimpl_->response_body_limit);
  if (selected_limit == 0) {
    throw std::invalid_argument("HTTP response body limit must be positive");
  }
  auto executor = co_await asio::this_coro::executor;
  auto driver = pimpl_->driver_for(executor);
  auto value =
      curl_request(pimpl_->config, pimpl_->proxy, detail::CurlHttpMethod::Get,
                   path, {}, headers, selected_limit);
  try {
    auto response = co_await driver->perform(std::move(value));
    pimpl_->connected = true;
    co_return make_response(std::move(response));
  } catch (const boost::system::system_error &error) {
    pimpl_->connected = false;
    OBCX_WARN("HTTP GET request failed");
    throw_request_error("GET", error.code(),
                        HttpRequestSubmissionState::PossiblySubmitted);
  } catch (const HttpClientError &) {
    pimpl_->connected = false;
    throw;
  } catch (const std::exception &) {
    pimpl_->connected = false;
    OBCX_WARN("HTTP GET request could not be prepared");
    throw HttpClientError("HTTP GET request could not be prepared",
                          HttpRequestSubmissionState::DefinitelyNotSubmitted);
  }
}

auto HttpClient::head(std::string_view path,
                      const std::map<std::string, std::string> &headers)
    -> asio::awaitable<HttpResponse> {
  auto executor = co_await asio::this_coro::executor;
  auto driver = pimpl_->driver_for(executor);
  auto value =
      curl_request(pimpl_->config, pimpl_->proxy, detail::CurlHttpMethod::Head,
                   path, {}, headers, pimpl_->response_body_limit);
  try {
    auto response = co_await driver->perform(std::move(value));
    pimpl_->connected = true;
    co_return make_response(std::move(response));
  } catch (const boost::system::system_error &error) {
    pimpl_->connected = false;
    OBCX_WARN("HTTP HEAD request failed");
    throw_request_error("HEAD", error.code(),
                        HttpRequestSubmissionState::PossiblySubmitted);
  } catch (const HttpClientError &) {
    pimpl_->connected = false;
    throw;
  } catch (const std::exception &) {
    pimpl_->connected = false;
    OBCX_WARN("HTTP HEAD request could not be prepared");
    throw HttpClientError("HTTP HEAD request could not be prepared",
                          HttpRequestSubmissionState::DefinitelyNotSubmitted);
  }
}

void HttpClient::set_timeout(const std::chrono::milliseconds timeout) {
  if (timeout.count() <= 0) {
    throw std::invalid_argument("HTTP timeout must be positive");
  }
  pimpl_->config.connect_timeout = timeout;
}

void HttpClient::set_response_body_limit(const std::uint64_t bytes) {
  if (bytes == 0) {
    throw std::invalid_argument("HTTP response body limit must be positive");
  }
  pimpl_->response_body_limit = bytes;
}

auto HttpClient::response_body_limit() const -> std::uint64_t {
  return pimpl_->response_body_limit;
}

auto HttpClient::is_connected() const -> bool {
  return pimpl_->connected.load();
}

auto HttpClient::get_timeout() const -> std::chrono::milliseconds {
  return pimpl_->config.connect_timeout;
}

auto HttpClient::get_host() const -> const std::string & {
  return pimpl_->config.host;
}

auto HttpClient::get_port() const -> std::uint16_t {
  return pimpl_->config.port;
}

auto HttpClient::use_ssl() const -> bool {
  return pimpl_->config.port == 443 || pimpl_->config.use_ssl;
}

auto HttpClient::get_ssl_context() const -> ssl::context * { return nullptr; }

void HttpClient::close() {
  pimpl_->connected = false;
  pimpl_->close_drivers();
  OBCX_INFO("HTTP client closed");
}

} // namespace obcx::network
