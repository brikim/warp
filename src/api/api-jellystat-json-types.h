#pragma once

#include <glaze/core/common.hpp>
#include <optional>
#include <string>
#include <vector>

namespace warp
{
   struct JsonJellystatHistoryItem
   {
      std::string name;
      std::string id;
      std::string user;
      std::string watchTime;
      std::optional<std::string> seriesName;
      std::optional<std::string> episodeId;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "NowPlayingItemName", &JsonJellystatHistoryItem::name,
            "NowPlayingItemId", &JsonJellystatHistoryItem::id,
            "UserName", &JsonJellystatHistoryItem::user,
            "ActivityDateInserted", &JsonJellystatHistoryItem::watchTime,
            "SeriesName", &JsonJellystatHistoryItem::seriesName,
            "EpisodeId", &JsonJellystatHistoryItem::episodeId);
         // clang-format on
      };
   };

   struct JsonJellystatHistoryItems
   {
      std::vector<JsonJellystatHistoryItem> items;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "results", &JsonJellystatHistoryItems::items);
         // clang-format on
      };
   };
} // namespace warp