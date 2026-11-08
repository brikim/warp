#pragma once

#include <cstdint>
#include <glaze/glaze.hpp>
#include <string>

namespace warp
{
   struct JsonTautulliMonitorInfo
   {
      int32_t movie_watched_percent{0};

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "movie_watched_percent", &JsonTautulliMonitorInfo::movie_watched_percent);
         // clang-format on
      };
   };

   struct JsonTautulliServerInfo
   {
      std::string pms_name;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "pms_name", &JsonTautulliServerInfo::pms_name);
         // clang-format on
      };
   };

   struct JsonUserInfo
   {
      std::string username;
      int32_t user_id{0};
      std::string friendly_name;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "username", &JsonUserInfo::username,
            "user_id", &JsonUserInfo::user_id,
            "friendly_name", &JsonUserInfo::friendly_name);
         // clang-format on
      };
   };

   struct JsonTautulliHistoryItem
   {
      std::string title;
      std::string full_title;
      int64_t date{0};
      int64_t rating_key{0};
      int64_t stopped{0};
      int32_t percent_complete{0};
      int32_t live{0};

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "title", &JsonTautulliHistoryItem::title,
            "full_title", &JsonTautulliHistoryItem::full_title,
            "date", &JsonTautulliHistoryItem::date,
            "rating_key", &JsonTautulliHistoryItem::rating_key,
            "stopped", &JsonTautulliHistoryItem::stopped,
            "percent_complete", &JsonTautulliHistoryItem::percent_complete,
            "live", &JsonTautulliHistoryItem::live);
         // clang-format on
      };
   };

   struct JsonTautulliHistoryData
   {
      std::vector<JsonTautulliHistoryItem> data;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "data", &JsonTautulliHistoryData::data);
         // clang-format on
      };
   };

   template <typename T>
   struct JsonTautulliResponse
   {
      struct InternalData
      {
         std::string result; // "success" or "error"
         T data;             // This is the actual object/vector you want

         struct glaze
         {
            using T_int = InternalData;
            // clang-format off
            static constexpr auto value = glz::object(
               "result", &T_int::result,
               "data", &T_int::data);
            // clang-format on
         };
      };

      InternalData response;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "response", &JsonTautulliResponse::response);
         // clang-format on
      };
   };
} // namespace warp