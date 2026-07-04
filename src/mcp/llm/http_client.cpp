#include "mcp/llm/http_client.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#endif

#include <httplib.h>

#include <chrono>
#include <thread>

namespace mothprobe::mcp::llm {
namespace {

struct UrlParts {
  bool https = false;
  std::string host;
  std::string path;
};

bool ShouldRetry(int status) {
  return status == 429 || status == 500 || status == 502 || status == 503;
}

void Backoff(int retry_index) {
  const int delay_ms = 400 * (1 << retry_index);
  std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
}

UrlParts ParseUrl(std::string url) {
  UrlParts out;
  out.https = url.rfind("https://", 0) == 0;
  const auto scheme = url.find("://");
  if (scheme != std::string::npos) url = url.substr(scheme + 3);
  const auto slash = url.find('/');
  out.host = slash == std::string::npos ? url : url.substr(0, slash);
  out.path = slash == std::string::npos ? "/" : url.substr(slash);
  return out;
}

#ifdef _WIN32
std::wstring Wide(const std::string& text) {
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
  if (!out.empty()) out.pop_back();
  return out;
}

HttpResponse WinHttpRequest(const std::wstring& method, const UrlParts& url,
                            const Headers& headers, const std::string& body) {
  HINTERNET session = WinHttpOpen(L"MothProbe/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) return {false, 0, "", "WinHTTP session failed", 1};
  HINTERNET connect = WinHttpConnect(session, Wide(url.host).c_str(),
                                     INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET request = connect ? WinHttpOpenRequest(connect, method.c_str(), Wide(url.path).c_str(),
                                                   nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   WINHTTP_FLAG_SECURE)
                              : nullptr;
  std::wstring header_text = L"Content-Type: application/json\r\n";
  for (const auto& [key, value] : headers) {
    header_text += Wide(key + ": " + value + "\r\n");
  }
  BOOL ok = request && WinHttpSendRequest(
                             request, header_text.c_str(), static_cast<DWORD>(header_text.size()),
                             body.empty() ? WINHTTP_NO_REQUEST_DATA
                                          : const_cast<char*>(body.data()),
                             static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
  ok = ok && WinHttpReceiveResponse(request, nullptr);
  DWORD status = 0;
  DWORD status_size = sizeof(status);
  if (request) {
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        nullptr, &status, &status_size, nullptr);
  }
  std::string response_body;
  if (ok) {
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
      std::string chunk(available, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
      chunk.resize(read);
      response_body += chunk;
    }
  }
  if (request) WinHttpCloseHandle(request);
  if (connect) WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return {ok != FALSE, static_cast<int>(status), response_body,
          ok ? std::string{} : std::string("WinHTTP request failed"), 1};
}
#endif

template <typename Fn>
HttpResponse WithRetries(Fn&& request) {
  constexpr int kMaxRetries = 3;
  HttpResponse last;
  for (int attempt = 1; attempt <= kMaxRetries + 1; ++attempt) {
    last = request();
    last.attempts = attempt;
    if (last.ok && !ShouldRetry(last.status)) return last;
    if (!last.ok) return last;
    if (attempt <= kMaxRetries) Backoff(attempt - 1);
  }
  return last;
}

}  // namespace

HttpResponse PostJson(const std::string& full_url, const Headers& headers, const std::string& body) {
  const auto url = ParseUrl(full_url);
  return WithRetries([&]() -> HttpResponse {
    httplib::Headers h;
    for (const auto& [key, value] : headers) h.emplace(key, value);
    if (url.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
      httplib::SSLClient client(url.host);
      client.set_read_timeout(60, 0);
      client.set_connection_timeout(20, 0);
      auto response = client.Post(url.path, h, body, "application/json");
      if (!response) return {false, 0, "", httplib::to_string(response.error()), 1};
      return {true, response->status, response->body, "", 1};
#elif defined(_WIN32)
      return WinHttpRequest(L"POST", url, headers, body);
#else
      return {false, 0, "", "HTTPS requires OpenSSL support", 1};
#endif
    }
    httplib::Client client("http://" + url.host);
    client.set_read_timeout(60, 0);
    client.set_connection_timeout(20, 0);
    auto response = client.Post(url.path, h, body, "application/json");
    if (!response) return {false, 0, "", httplib::to_string(response.error()), 1};
    return {true, response->status, response->body, "", 1};
  });
}

HttpResponse GetJson(const std::string& full_url, const Headers& headers) {
  const auto url = ParseUrl(full_url);
  return WithRetries([&]() -> HttpResponse {
    httplib::Headers h;
    for (const auto& [key, value] : headers) h.emplace(key, value);
    if (url.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
      httplib::SSLClient client(url.host);
      client.set_read_timeout(60, 0);
      client.set_connection_timeout(20, 0);
      auto response = client.Get(url.path, h);
      if (!response) return {false, 0, "", httplib::to_string(response.error()), 1};
      return {true, response->status, response->body, "", 1};
#elif defined(_WIN32)
      return WinHttpRequest(L"GET", url, headers, "");
#else
      return {false, 0, "", "HTTPS requires OpenSSL support", 1};
#endif
    }
    httplib::Client client("http://" + url.host);
    client.set_read_timeout(60, 0);
    client.set_connection_timeout(20, 0);
    auto response = client.Get(url.path, h);
    if (!response) return {false, 0, "", httplib::to_string(response.error()), 1};
    return {true, response->status, response->body, "", 1};
  });
}

}  // namespace mothprobe::mcp::llm
