#include "warp/http/http-client.h"

#include "http/http-utils.h"
#include "warp/types.h"

#include <httplib.h>

#include <cstdint>
#include <format>

namespace warp
{
   namespace
   {
      [[nodiscard]] std::string AddHttpParam(const std::string_view url, const HttpParams& params)
      {
         if (params.empty()) return std::string(url);

         std::string urlStr(url);

         bool hasQuery = (url.find('?') != std::string::npos);
         bool lastIsSeparator = !url.empty() && (url.back() == '?' || url.back() == '&');
         for (const auto& [key, value] : params)
         {
            if (!lastIsSeparator)
            {
               urlStr += hasQuery ? '&' : '?';
            }
            urlStr += key;
            urlStr += '=';
            urlStr += GetPercentEncoded(value);

            hasQuery = true;
            lastIsSeparator = false;
         }
         return urlStr;
      }
   }

   struct HttpClient::HttpClientImpl
   {
      std::string url_;
      httplib::Client client_;

      HttpClientImpl(const std::string_view url);

      [[nodiscard]] Error ConvertError(httplib::Error httpError) const;
      [[nodiscard]] std::string ErrorToString(const Error error) const;

      [[nodiscard]] httplib::Headers GetHttpLibHeaders(const Headers& headers);
      [[nodiscard]] httplib::Params GetHttpLibParams(const HttpParams& params);
      Response GetInvalidResponse(const httplib::Result& res) const;
   };

   HttpClient::HttpClientImpl::HttpClientImpl(const std::string_view url)
      : url_(url)
      , client_(url_)
   {
      constexpr time_t timeoutSec{5};
      client_.set_connection_timeout(timeoutSec);

      constexpr time_t readWritetimeoutSec{10};
      client_.set_read_timeout(readWritetimeoutSec);
      client_.set_write_timeout(readWritetimeoutSec);
      client_.set_keep_alive(true);
   }

   HttpClient::HttpClient(const std::string_view url)
      : pimpl_(std::make_unique<HttpClientImpl>(url))
   {}

   HttpClient::~HttpClient() = default;

   std::string_view HttpClient::GetUrl() const
   {
      return pimpl_->url_;
   }

   std::string HttpClient::BuildHttpPath(const std::string_view base,
                                         const std::string_view path,
                                         const std::optional<std::string_view> loginToken,
                                         const std::optional<std::string_view> loginKey) const
   {
      if (!loginToken || !loginKey || loginToken->empty() || loginKey->empty())
      {
         return std::format("{}{}", base, path);
      }
      else
      {
         char separator = (path.find('?') == std::string_view::npos) ? '?' : '&';
         return std::format("{}{}{}{}={}",
                            base,
                            path,
                            separator,
                            loginToken.value(),
                            GetPercentEncoded(loginKey.value()));
      }
   }

   std::string HttpClient::BuildHttpParamsPath(const std::string_view base,
                                               const std::string_view path,
                                               const HttpParams& params,
                                               const std::optional<std::string_view> loginToken,
                                               const std::optional<std::string_view> loginKey) const
   {
      auto httpPath = BuildHttpPath(base, path, loginToken, loginKey);
      return AddHttpParam(httpPath, params);
   }

   Response HttpClient::Get(const std::string& path, const Headers& headers)
   {
      auto res = pimpl_->client_.Get(path, pimpl_->GetHttpLibHeaders(headers));

      if (!res) return pimpl_->GetInvalidResponse(res);

      return Response{
         .status = res->status,
         .reason = std::move(res->reason),
         .body = std::move(res->body),
         .error = pimpl_->ConvertError(res.error())
      };
   }

   Response HttpClient::Post(const std::string& path, const Headers& headers)
   {
      auto res = pimpl_->client_.Post(path, pimpl_->GetHttpLibHeaders(headers));

      if (!res) return pimpl_->GetInvalidResponse(res);

      return Response{
         .status = res->status,
         .reason = std::move(res->reason),
         .body = std::move(res->body),
         .error = pimpl_->ConvertError(res.error())
      };
   }

   Response HttpClient::Post(const std::string& path, const Headers& headers, const std::string& body, const std::string& contentType)
   {
      auto res = pimpl_->client_.Post(path, pimpl_->GetHttpLibHeaders(headers), body, contentType);

      if (!res) return pimpl_->GetInvalidResponse(res);

      return Response{
         .status = res->status,
         .reason = std::move(res->reason),
         .body = std::move(res->body),
         .error = pimpl_->ConvertError(res.error())
      };
   }

   Response HttpClient::Post(const std::string& path, const HttpParams& params)
   {
      auto res = pimpl_->client_.Post(path, pimpl_->GetHttpLibParams(params));

      if (!res) return pimpl_->GetInvalidResponse(res);

      return Response{
         .status = res->status,
         .reason = std::move(res->reason),
         .body = std::move(res->body),
         .error = pimpl_->ConvertError(res.error())
      };
   }

   Response HttpClient::Post(const std::string& path, const Headers& headers, const HttpParams& params)
   {
      auto res = pimpl_->client_.Post(path, pimpl_->GetHttpLibHeaders(headers), pimpl_->GetHttpLibParams(params));

      if (!res) return pimpl_->GetInvalidResponse(res);

      return Response{
         .status = res->status,
         .reason = std::move(res->reason),
         .body = std::move(res->body),
         .error = pimpl_->ConvertError(res.error())
      };
   }

   Response HttpClient::Delete(const std::string& path, const Headers& headers)
   {
      auto res = pimpl_->client_.Delete(path, pimpl_->GetHttpLibHeaders(headers));

      if (!res) return pimpl_->GetInvalidResponse(res);

      return Response{
         .status = res->status,
         .reason = std::move(res->reason),
         .body = std::move(res->body),
         .error = pimpl_->ConvertError(res.error())
      };
   }

   httplib::Headers HttpClient::HttpClientImpl::GetHttpLibHeaders(const Headers& headers)
   {
      httplib::Headers httpHeaders;
      for (const auto& header : headers)
      {
         httpHeaders.emplace(header.first, header.second);
      }
      return httpHeaders;
   }

   httplib::Params HttpClient::HttpClientImpl::GetHttpLibParams(const HttpParams& params)
   {
      httplib::Params httpParams;
      for (const auto& param : params)
      {
         httpParams.emplace(param.first, param.second);
      }
      return httpParams;
   }

   Response HttpClient::HttpClientImpl::GetInvalidResponse(const httplib::Result& res) const
   {
      return Response{
          .status = VALID_HTTP_RESPONSE_MAX,
          .reason = "HTTP returned no result",
          .body = "",
          .error = ConvertError(res.error())
      };
   }

   std::optional<std::string> HttpClient::GetHttpResponseStr(std::string_view name, const Response& response)
   {
      if (response.error == Error::Success && response.status < VALID_HTTP_RESPONSE_MAX)
      {
         return std::nullopt;
      }

      if (response.error != Error::Success)
      {
         return pimpl_->ErrorToString(response.error);
      }
      else
      {
         return std::format("Status {}: {} - {}", response.status, response.reason, response.body);
      }
   }

   Error HttpClient::HttpClientImpl::ConvertError(httplib::Error httpError) const
   {
      switch (httpError)
      {
         case httplib::Error::Success: return Error::Success;
         case httplib::Error::Unknown: return Error::Unknown;
         case httplib::Error::Connection: return Error::Connection;
         case httplib::Error::BindIPAddress: return Error::BindIPAddress;
         case httplib::Error::Read: return Error::Read;
         case httplib::Error::Write: return Error::Write;
         case httplib::Error::ExceedRedirectCount: return Error::ExceedRedirectCount;
         case httplib::Error::Canceled: return Error::Canceled;
         case httplib::Error::SSLConnection: return Error::SSLConnection;
         case httplib::Error::SSLLoadingCerts: return Error::SSLLoadingCerts;
         case httplib::Error::SSLServerVerification: return Error::SSLServerVerification;
         case httplib::Error::SSLServerHostnameVerification: return Error::SSLServerHostnameVerification;
         case httplib::Error::UnsupportedMultipartBoundaryChars: return Error::UnsupportedMultipartBoundaryChars;
         case httplib::Error::Compression: return Error::Compression;
         case httplib::Error::ConnectionTimeout: return Error::ConnectionTimeout;
         case httplib::Error::ProxyConnection: return Error::ProxyConnection;
         case httplib::Error::ConnectionClosed: return Error::ConnectionClosed;
         case httplib::Error::Timeout: return Error::Timeout;
         case httplib::Error::ResourceExhaustion: return Error::ResourceExhaustion;
         case httplib::Error::TooManyFormDataFiles: return Error::TooManyFormDataFiles;
         case httplib::Error::ExceedMaxPayloadSize: return Error::ExceedMaxPayloadSize;
         case httplib::Error::ExceedUriMaxLength: return Error::ExceedUriMaxLength;
         case httplib::Error::ExceedMaxSocketDescriptorCount: return Error::ExceedMaxSocketDescriptorCount;
         case httplib::Error::InvalidRequestLine: return Error::InvalidRequestLine;
         case httplib::Error::InvalidHTTPMethod: return Error::InvalidHTTPMethod;
         case httplib::Error::InvalidHTTPVersion: return Error::InvalidHTTPVersion;
         case httplib::Error::InvalidHeaders: return Error::InvalidHeaders;
         case httplib::Error::MultipartParsing: return Error::MultipartParsing;
         case httplib::Error::OpenFile: return Error::OpenFile;
         case httplib::Error::Listen: return Error::Listen;
         case httplib::Error::GetSockName: return Error::GetSockName;
         case httplib::Error::UnsupportedAddressFamily: return Error::UnsupportedAddressFamily;
         case httplib::Error::HTTPParsing: return Error::HTTPParsing;
         case httplib::Error::InvalidRangeHeader: return Error::InvalidRangeHeader;
         case httplib::Error::SSLPeerCouldBeClosed_: return Error::SSLPeerCouldBeClosed_;
         default: return Error::Unknown;
      }
   }

   std::string HttpClient::HttpClientImpl::ErrorToString(const Error error) const
   {
      switch (error)
      {
         case Error::Success: return "Success (no error)";
         case Error::Unknown: return "Unknown";
         case Error::Connection: return "Could not establish connection";
         case Error::BindIPAddress: return "Failed to bind IP address";
         case Error::Read: return "Failed to read connection";
         case Error::Write: return "Failed to write connection";
         case Error::ExceedRedirectCount: return "Maximum redirect count exceeded";
         case Error::Canceled: return "Connection handling canceled";
         case Error::SSLConnection: return "SSL connection failed";
         case Error::SSLLoadingCerts: return "SSL certificate loading failed";
         case Error::SSLServerVerification: return "SSL server verification failed";
         case Error::SSLServerHostnameVerification: return "SSL server hostname verification failed";
         case Error::UnsupportedMultipartBoundaryChars: return "Unsupported HTTP multipart boundary characters";
         case Error::Compression: return "Compression failed";
         case Error::ConnectionTimeout: return "Connection timed out";
         case Error::ProxyConnection: return "Proxy connection failed";
         case Error::ConnectionClosed: return "Connection closed by server";
         case Error::Timeout: return "Read timeout";
         case Error::ResourceExhaustion: return "Resource exhaustion";
         case Error::TooManyFormDataFiles: return "Too many form data files";
         case Error::ExceedMaxPayloadSize: return "Exceeded maximum payload size";
         case Error::ExceedUriMaxLength: return "Exceeded maximum URI length";
         case Error::ExceedMaxSocketDescriptorCount: return "Exceeded maximum socket descriptor count";
         case Error::InvalidRequestLine: return "Invalid request line";
         case Error::InvalidHTTPMethod: return "Invalid HTTP method";
         case Error::InvalidHTTPVersion: return "Invalid HTTP version";
         case Error::InvalidHeaders: return "Invalid headers";
         case Error::MultipartParsing: return "Multipart parsing failed";
         case Error::OpenFile: return "Failed to open file";
         case Error::Listen: return "Failed to listen on socket";
         case Error::GetSockName: return "Failed to get socket name";
         case Error::UnsupportedAddressFamily: return "Unsupported address family";
         case Error::HTTPParsing: return "HTTP parsing failed";
         case Error::InvalidRangeHeader: return "Invalid Range header";
         default: return "Error condition Unknown";
      }

      return "Invalid";
   }
}