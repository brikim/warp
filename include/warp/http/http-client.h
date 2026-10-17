#pragma once

#include "warp/http/http-response.h"
#include "warp/types.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace warp
{
   using HttpParams = std::vector<std::pair<std::string_view, std::string_view>>;

   class HttpClient
   {
   public:
      HttpClient(const std::string_view url);
      virtual ~HttpClient();

      [[nodiscard]] std::string_view GetUrl() const;

      [[nodiscard]] Response Get(const std::string& path, const Headers& headers);
      [[nodiscard]] Response Post(const std::string& path, const Headers& headers);
      [[nodiscard]] Response Post(const std::string& path, const Headers& headers, const std::string& body, const std::string& contentType);
      [[nodiscard]] Response Post(const std::string& path, const HttpParams& params);
      [[nodiscard]] Response Post(const std::string& path, const Headers& headers, const HttpParams& params);
      [[nodiscard]] Response Delete(const std::string& path, const Headers& headers);

      [[nodiscard]] std::string BuildHttpPath(const std::string_view base,
                                              const std::string_view path,
                                              const std::optional<std::string_view> loginToken = std::nullopt,
                                              const std::optional<std::string_view> loginKey = std::nullopt) const;
      [[nodiscard]] std::string BuildHttpParamsPath(const std::string_view base,
                                                    const std::string_view path,
                                                    const HttpParams& params,
                                                    const std::optional<std::string_view> loginToken = std::nullopt,
                                                    const std::optional<std::string_view> loginKey = std::nullopt) const;

      [[nodiscard]] std::optional<std::string> GetHttpResponseStr(std::string_view name, const Response& response);

   private:
      struct HttpClientImpl;
      friend struct HttpClientImpl;
      std::unique_ptr<HttpClientImpl> pimpl_;
   };
}