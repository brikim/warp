#include "warp/api/api-base.h"

#include "http/http-utils.h"
#include "warp/http/http-client.h"
#include "warp/log/log-utils.h"
#include "warp/types.h"

#include <cstdint>
#include <format>

namespace warp
{
   namespace
   {
      constexpr int32_t CRON_QUICK_CHECK_SECOND_START{5};
      constexpr int32_t CRON_QUICK_CHECK_SECOND_INCREMENT{5};
      constexpr int32_t CRON_QUICK_CHECK_MINUTES{10};

      constexpr int32_t CRON_FULL_CHECK_MINUTE_START{31};
      constexpr int32_t CRON_FULL_CHECK_MINUTE_INCREMENT{2};
      constexpr int32_t CRON_FULL_CHECK_HOUR{3};
   }

   struct ApiBase::ApiBaseImpl
   {
      std::string name_;
      std::string prettyName_;
      std::string apiKey_;
      HttpClient client_;

      ApiBaseImpl(const ApiBaseData& data);
   };

   ApiBase::ApiBaseImpl::ApiBaseImpl(const ApiBaseData& data)
      : name_(data.name)
      , prettyName_(data.prettyName)
      , apiKey_(data.apiKey)
      , client_(data.url)
   {}

   ApiBase::ApiBase(const ApiBaseData& data)
      : Base(data.className, data.ansiiCode, data.name)
      , pimpl_(std::make_unique<ApiBaseImpl>(data))
   {}

   ApiBase::~ApiBase() = default;

   std::optional<std::vector<Task>> ApiBase::GetTaskList()
   {
      return std::nullopt;
   }

   void ApiBase::Shutdown()
   {
      // do nothing by default
   }

   std::string_view ApiBase::GetName() const
   {
      return pimpl_->name_;
   }

   std::string_view ApiBase::GetPrettyName() const
   {
      return pimpl_->prettyName_;
   }

   std::string_view ApiBase::GetApiKey() const
   {
      return pimpl_->apiKey_;
   }

   std::string_view ApiBase::GetUrl() const
   {
      return pimpl_->client_.GetUrl();
   }

   std::string ApiBase::GetNextCronQuickTime() const
   {
      // Start at the 5 second mark
      static auto seconds{CRON_QUICK_CHECK_SECOND_START};

      auto useSeconds = seconds;
      seconds += CRON_QUICK_CHECK_SECOND_INCREMENT;

      return std::format("{} */{} * * * *", useSeconds, CRON_QUICK_CHECK_MINUTES);
   }

   std::string ApiBase::GetNextCronFullTime() const
   {
      static auto minutes{CRON_FULL_CHECK_MINUTE_START};

      auto minutesToUse = minutes;
      minutes += CRON_FULL_CHECK_MINUTE_INCREMENT;

      return std::format("0 {} {} * * *", minutesToUse, CRON_FULL_CHECK_HOUR);
   }

   std::string ApiBase::BuildApiPath(std::string_view path) const
   {
      return pimpl_->client_.BuildHttpPath(GetApiBase(), path, GetApiTokenName(), GetApiKey());
   }

   std::string ApiBase::BuildApiParamsPath(std::string_view path, const HttpParams& params) const
   {
      return pimpl_->client_.BuildHttpParamsPath(GetApiBase(), path, params, GetApiTokenName(), GetApiKey());
   }

   bool ApiBase::IsHttpSuccess(std::string_view name, const Response& response, bool log)
   {
      auto errorStr = pimpl_->client_.GetHttpResponseStr(name, response);
      if (!errorStr)
      {
         // Everything passed
         return true;
      }

      if (log) LogWarning("{} - HTTP error {}", name, warp::GetTag("error", *errorStr));

      return false;
   }

   Response ApiBase::Get(const std::string& path, const Headers& headers)
   {
      return pimpl_->client_.Get(path, headers);
   }

   Response ApiBase::Post(const std::string& path, const Headers& headers)
   {
      return pimpl_->client_.Post(path, headers);
   }

   Response ApiBase::Post(const std::string& path, const Headers& headers, const std::string& body, const std::string& contentType)
   {
      return pimpl_->client_.Post(path, headers, body, contentType);
   }

   Response ApiBase::Delete(const std::string& path, const Headers& headers)
   {
      return pimpl_->client_.Delete(path, headers);
   }
}