#pragma once

#include "warp/api/api-base.h"
#include "warp/api/api-plex-types.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace warp
{
   class PlexApi : public ApiBase
   {
   public:
      PlexApi(std::string_view appName,
              std::string_view version,
              const ServerConfig& serverConfig,
              const ServerPlexOptions& options);
      ~PlexApi() override;

      [[nodiscard]] std::optional<std::vector<Task>> GetTaskList() override;

      // Returns true if the server is reachable and the API key is valid
      [[nodiscard]] bool GetValid() override;
      [[nodiscard]] const std::filesystem::path& GetMediaPath() const;
      [[nodiscard]] std::optional<std::string> GetServerReportedName() override;
      [[nodiscard]] std::optional<std::string> GetTracearrServerName() const;
      [[nodiscard]] std::optional<std::string> GetLibraryId(std::string_view libraryName) const;

      [[nodiscard]] std::optional<PlexSearchResults> GetItemInfoByPathWithToken(std::string_view userToken, const std::filesystem::path& filePath);
      [[nodiscard]] std::optional<PlexSearchResults> GetItemInfoByPathWithUserName(std::string_view userName, const std::filesystem::path& filePath);

      [[nodiscard]] std::optional<std::filesystem::path> GetItemPath(std::string_view id);
      [[nodiscard]] std::unordered_map<std::string, std::filesystem::path> GetItemsPaths(std::span<const std::string_view> ids);
      [[nodiscard]] std::vector<PlexSearchResult> GetItemsByTmdbIds(std::span<const std::string_view> tmdbIds);
      [[nodiscard]] std::vector<PlexSearchResult> GetItemsByTmdbIds(std::string_view libraryName, std::span<const std::string_view> tmdbIds);

      // Returns if the collection in the library is valid on this server
      [[nodiscard]] bool GetCollectionValid(std::string_view library, std::string_view collection);

      // Returns the collection information for the collection in the library
      [[nodiscard]] std::optional<PlexCollection> GetCollection(std::string_view library, std::string_view collectionName);
      [[nodiscard]] std::optional<PlexCollectionSort> GetCollectionSort(
         std::string_view library,
         std::string_view collectionName);

      bool CreateCollection(
         std::string_view library,
         std::string_view collectionName,
         const std::vector<std::string>& itemRatingKeys);
      bool SetCollectionSort(
         std::string_view library,
         std::string_view collectionName,
         PlexCollectionSort sort);
      bool SetCollectionTitleSort(
         std::string_view library,
         std::string_view collectionName,
         std::string_view titleSort);
      bool AddCollectionItems(
         std::string_view library,
         std::string_view collectionName,
         const std::vector<std::string>& itemRatingKeys);
      bool RemoveCollectionItems(
         std::string_view library,
         std::string_view collectionName,
         const std::vector<std::string>& itemRatingKeys);
      bool MoveCollectionItem(
         std::string_view library,
         std::string_view collectionName,
         std::string_view itemRatingKey,
         std::optional<std::string_view> afterRatingKey = std::nullopt);

      // Returns if a user token is available for the given user name
      [[nodiscard]] bool GetUserTokenAvailable(std::string_view userName) const;

      // Tell Plex to scan the passed in library
      void SetLibraryScan(std::string_view libraryId);
      void SetLibraryScanPath(std::string_view libraryId, const std::filesystem::path& path);

      bool SetPlayedByUserToken(std::string_view userToken, std::string_view ratingKey, int64_t locationMs);
      bool SetPlayedByUserName(std::string_view userName, std::string_view ratingKey, int64_t locationMs);

      bool SetWatchedByUserToken(std::string_view userToken, std::string_view ratingKey);
      bool SetWatchedByUserName(std::string_view userName, std::string_view ratingKey);

   protected:
      std::string_view GetApiBase() const override;
      std::string_view GetApiTokenName() const override;

   private:
      struct PlexApiImpl;
      friend struct PlexApiImpl;
      [[nodiscard]] std::vector<PlexSearchResult> GetItemsByTmdbIdsImpl(std::span<const std::string_view> tmdbIds, std::optional<std::string_view> libraryName);

      std::unique_ptr<PlexApiImpl> pimpl_;
   };
} // namespace warp
