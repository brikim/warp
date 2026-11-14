#pragma once

#include <glaze/core/common.hpp>
#include <string>
#include <vector>

namespace warp
{
   struct JsonTracearrServer
   {
      std::string id;
      std::string name;
      std::string type;
      std::string version;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "server_id", &JsonTracearrServer::id,
            "server_name", &JsonTracearrServer::name,
            "server_type", &JsonTracearrServer::type,
            "version", &JsonTracearrServer::version);
         // clang-format on
      };
   };

   struct JsonTracearrServers
   {
      std::string version;
      std::vector<JsonTracearrServer> servers;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "tracearr_version", &JsonTracearrServers::version,
            "data", &JsonTracearrServers::servers);
         // clang-format on
      };
   };

   struct JsonTracearrHistoryUser
   {
      std::string id;
      std::string userName;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "id", &JsonTracearrHistoryUser::id,
            "username", &JsonTracearrHistoryUser::userName);
         // clang-format on
      };
   };

   struct JsonTracearrHistoryItem
   {
      std::string serverId;
      std::string serverName;
      std::string serverType;
      std::string state;
      std::string mediaTitle;
      std::string mediaType;
      std::optional<std::string> showTitle;
      std::optional<int> seasonNumber;
      std::optional<int> episodeNumber;
      std::optional<double> percentComplete;
      std::string startedAt;
      std::optional<std::string> stoppedAt;
      bool watched{false};
      std::string serverRatingKey;
      std::optional<std::string> libraryId;
      JsonTracearrHistoryUser user;

      struct glaze
      {
         // clang-format off
         // Glaze knows how to handle chrono types automatically
         static constexpr auto value = glz::object(
            "server_id", &JsonTracearrHistoryItem::serverId,
            "server_name", &JsonTracearrHistoryItem::serverName,
            "server_type", &JsonTracearrHistoryItem::serverType,
            "state", &JsonTracearrHistoryItem::state,
            "media_title", &JsonTracearrHistoryItem::mediaTitle,
            "media_type", &JsonTracearrHistoryItem::mediaType,
            "show_title", &JsonTracearrHistoryItem::showTitle,
            "season_number", &JsonTracearrHistoryItem::seasonNumber,
            "episode_number", &JsonTracearrHistoryItem::episodeNumber,
            "percent_complete", &JsonTracearrHistoryItem::percentComplete,
            "started_at", &JsonTracearrHistoryItem::startedAt,
            "stopped_at", &JsonTracearrHistoryItem::stoppedAt,
            "watched", &JsonTracearrHistoryItem::watched,
            "rating_key", &JsonTracearrHistoryItem::serverRatingKey,
            "library_id", &JsonTracearrHistoryItem::libraryId,
            "user", &JsonTracearrHistoryItem::user);
         // clang-format on
      };
   };

   struct JsonTracearrHistoryItems
   {
      std::vector<JsonTracearrHistoryItem> items;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "data", &JsonTracearrHistoryItems::items);
         // clang-format on
      };
   };

   struct JsonTracearrUserAccount
   {
      std::string serverId;
      std::string serverType;
      std::string serverUserId;
      std::string externalUserId;
      std::string externalUserName;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "server_id", &JsonTracearrUserAccount::serverId,
            "server_type", &JsonTracearrUserAccount::serverType,
            "server_user_id", &JsonTracearrUserAccount::serverUserId,
            "external_user_id", &JsonTracearrUserAccount::externalUserId,
            "username", &JsonTracearrUserAccount::externalUserName);
         // clang-format on
      };
   };

   struct JsonTracearrUser
   {
      std::string id;
      std::string name;
      std::optional<std::string> tracearrName;
      std::vector<JsonTracearrUserAccount> accounts;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "id", &JsonTracearrUser::id,
            "username", &JsonTracearrUser::name,
            "identity_name", &JsonTracearrUser::tracearrName,
            "accounts", &JsonTracearrUser::accounts);
         // clang-format on
      };
   };

   struct JsonTracearrUsers
   {
      std::vector<JsonTracearrUser> users;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "data", &JsonTracearrUsers::users);
         // clang-format on
      };
   };
} // namespace warp
