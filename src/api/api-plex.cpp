#include "warp/api/api-plex.h"

#include "api/api-plex-json-types.h"
#include "http/http-utils.h"
#include "warp/http/http-client.h"
#include "warp/log/log-utils.h"
#include "warp/types.h"
#include "warp/utils.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <glaze/glaze.hpp>
#include <mutex>
#include <optional>
#include <pugixml.hpp>
#include <ranges>
#include <shared_mutex>
#include <unordered_set>

namespace warp
{
   namespace
   {
      const std::string PLEX_RESOURCE_URL{"https://plex.tv"};

      constexpr std::string_view API_BASE{""};
      const std::string API_TOKEN_NAME{"X-Plex-Token"};

      const std::string API_PLEXTV_RESOURCES{"/api/resources"};
      const std::string API_PLEXTV_ADMIN_USER{"/api/v2/user"};
      const std::string API_PLEXTV_SERVERS("/api/servers");

      constexpr std::string_view API_SERVERS{"/servers"};
      constexpr std::string_view API_LIBRARIES{"/library/sections"};
      constexpr std::string_view API_LIBRARY_DATA{"/library/metadata"};

      struct PlexLibraryLookup
      {
         std::string name;
         std::string id;
         PlexSearchTypes type;
      };

      PlexSearchResult ToPlexSearchResult(JsonPlexMetadata& metadata, std::string_view libraryName)
      {
         PlexSearchResult result;
         if (metadata.showTitle)
         {
            result.title = std::move(*metadata.showTitle);
            result.title += " - ";
            result.title += metadata.title;
         }
         else
         {
            result.title = std::move(metadata.title);
         }

         result.ratingKey = std::move(metadata.ratingKey);
         result.libraryName = libraryName;
         result.durationMs = static_cast<int64_t>(metadata.duration);
         result.watched = metadata.viewCount && !metadata.viewOffset;

         constexpr std::string_view tmdbGuidPrefix{"tmdb://"};
         for (const auto& guid : metadata.guids)
         {
            if (!guid.id.starts_with(tmdbGuidPrefix))
               continue;

            const auto tmdbId = std::string_view(guid.id).substr(tmdbGuidPrefix.size());
            if (!tmdbId.empty() &&
                std::ranges::all_of(tmdbId, [](char digit) { return digit >= '0' && digit <= '9'; }) &&
                std::ranges::any_of(tmdbId, [](char digit) { return digit != '0'; }))
            {
               result.tmdbId = std::string(tmdbId);
               break;
            }
         }

         if (result.watched)
         {
            result.playbackPercentage = 100;
         }
         else if (result.durationMs > 0 && metadata.viewOffset)
         {
            result.playbackPercentage = std::lround(
               (static_cast<double>(*metadata.viewOffset) * 100.0) / static_cast<double>(result.durationMs));
         }

         for (auto& media : metadata.media)
         {
            for (auto& part : media.part)
               result.paths.emplace_back(std::move(part.file));
         }

         return result;
      }

   } // namespace

   struct PlexApi::PlexApiImpl
   {
      std::optional<std::string> tracearrServerName_;

      HttpClient plexTvClient_;
      Headers plexTvHeaders_;

      PlexApi& parent_;
      Headers headersNoToken_;
      Headers adminHeaders_;

      std::optional<std::string> machineIdentifier_;
      std::filesystem::path mediaPath_;
      bool enableCacheCollections_{false};
      bool enableCachePaths_{false};
      bool enableUserTokens_{false};

      mutable std::shared_mutex dataLock_;

      struct LibraryData
      {
         std::string id;
         std::string type;
         std::string agent;
         int64_t contentChangedAt{0};
         int64_t latestUpdateTime{0};
      };
      using PlexNameToLibraryMap = std::unordered_map<std::string, LibraryData, StringHash, std::equal_to<>>;
      PlexNameToLibraryMap libraries_;

      using PlexNameToIdMap = std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>;
      PlexNameToIdMap userTokens_;

      using PlexIdToPathMap = std::unordered_map<std::string, std::filesystem::path, StringHash, std::equal_to<>>;
      PlexIdToPathMap idToPathCache_;

      using PlexPathToIdMap = std::unordered_map<std::filesystem::path, std::string, PathHash, std::equal_to<>>;
      PlexPathToIdMap pathToIdCache_;

      std::vector<std::string> pathsSectionIds_;

      using PlexIdToIdMap = std::unordered_map<std::string, PlexNameToIdMap, StringHash, std::equal_to<>>;
      PlexIdToIdMap collections_;

      PlexApiImpl(PlexApi& p, std::string_view appName, std::string_view version, const ServerConfig& serverConfig);

      void EnableCacheCollections();
      void EnableCachePaths();
      void EnableUserTokens();

      // Returns if any of the libraries have changed
      void RebuildLibraryMap();
      void RebuildCollectionMap();
      void RebuildPathMap();
      void RebuildUserTokenMap();
      void RebuildMachineIdentifier();

      void CheckPathMap();
      bool CheckHttpSuccess(const warp::Response& res, std::string_view function);

      void UpdateCacheRequired(bool forceRefresh);
      void UpdateCacheCollections(bool forceRefresh);
      void UpdateCachePaths(bool forceRefresh);
      void UpdateUserTokens(bool forceRefresh);
      void RefreshCache(bool forceRefresh);

      [[nodiscard]] std::optional<std::string> GetTracearrServerName() const;
      [[nodiscard]] std::optional<std::string> GetLibraryId(std::string_view libraryName) const;

      // Returns the collection rating key
      [[nodiscard]] std::string GetCollectionId(std::string_view library, std::string_view collection);
      [[nodiscard]] std::optional<std::string> GetMachineIdentifier();
   };

   PlexApi::PlexApiImpl::PlexApiImpl(PlexApi& p, std::string_view appName, std::string_view version, const ServerConfig& serverConfig)
      : tracearrServerName_(serverConfig.tracearrServerName)
      , plexTvClient_(PLEX_RESOURCE_URL)
      , parent_(p)
      , mediaPath_(serverConfig.mediaPath)
   {
      // Setup the headers
      auto baseHeaders = Headers{
         // X-Plex-Client-Identifier: A stable UUID that uniquely identifies this application
         // to the Plex API. This must remain constant across runs so Plex recognizes warp
         // as a trusted client. Do not change unless intentionally re-registering with Plex.
         {"X-Plex-Client-Identifier", "6e7417e2-8d76-4b1f-9c23-018274959a37"},
         {"User-Agent", std::format("{}/{}", appName, version)}};

      headersNoToken_ = baseHeaders;
      headersNoToken_.emplace("Accept", APPLICATION_JSON);

      adminHeaders_ = headersNoToken_;
      adminHeaders_.emplace(API_TOKEN_NAME, parent_.GetApiKey());

      // Copy the default headers into the httplib version
      std::ranges::for_each(baseHeaders, [this](auto& header) {
         plexTvHeaders_.emplace(header.first, header.second);
      });
      plexTvHeaders_.emplace(API_TOKEN_NAME, parent_.GetApiKey());
      plexTvHeaders_.emplace("Accept", "application/xml");

      // Update required cache immediantly
      UpdateCacheRequired(true);
   }

   PlexApi::PlexApi(std::string_view appName,
                    std::string_view version,
                    const ServerConfig& serverConfig,
                    const ServerPlexOptions& options)
      : ApiBase(ApiBaseData{.name = serverConfig.serverName,
                            .url = serverConfig.url,
                            .apiKey = serverConfig.apiKey,
                            .className = "PlexApi",
                            .ansiiCode = ANSI_CODE_PLEX,
                            .prettyName = GetServerName(GetFormattedPlex(), serverConfig.serverName)})
      , pimpl_(std::make_unique<PlexApiImpl>(*this, appName, version, serverConfig))
   {
      if (options.enableCacheCollection)
         pimpl_->EnableCacheCollections();

      if (options.enableCachePaths)
         pimpl_->EnableCachePaths();

      if (options.enableUserTokens)
         pimpl_->EnableUserTokens();
   }

   PlexApi::~PlexApi() = default;

   void PlexApi::PlexApiImpl::EnableCacheCollections()
   {
      enableCacheCollections_ = true;
      UpdateCacheCollections(true);
   }

   void PlexApi::PlexApiImpl::EnableCachePaths()
   {
      enableCachePaths_ = true;
      UpdateCachePaths(true);
   }

   void PlexApi::PlexApiImpl::EnableUserTokens()
   {
      enableUserTokens_ = true;
      UpdateUserTokens(true);
   }

   std::optional<std::vector<Task>> PlexApi::GetTaskList()
   {
      std::vector<Task> tasks;

      auto& quickCheck = tasks.emplace_back();
      quickCheck.name = std::format("{} - Refresh Cache Quick", GetPrettyName());
      quickCheck.cronExpression = GetNextCronQuickTime();
      quickCheck.func = [this]() { pimpl_->RefreshCache(false); };

      auto& fullUpdate = tasks.emplace_back();
      fullUpdate.name = std::format("{} - Refresh Cache Full", GetPrettyName());
      fullUpdate.cronExpression = GetNextCronFullTime();
      fullUpdate.func = [this]() { pimpl_->RefreshCache(true); };

      return tasks;
   }

   std::string_view PlexApi::GetApiBase() const
   {
      return API_BASE;
   }

   // This returns empty string so the parent helper does not try to add the api token since this is inserted
   // into the header and not built into the url as a query param
   std::string_view PlexApi::GetApiTokenName() const
   {
      return "";
   }

   bool PlexApi::GetValid()
   {
      auto res = Get(BuildApiPath(API_SERVERS), pimpl_->adminHeaders_);
      return res.error == Error::Success && res.status < VALID_HTTP_RESPONSE_MAX;
   }

   const std::filesystem::path& PlexApi::GetMediaPath() const
   {
      return pimpl_->mediaPath_;
   }

   std::optional<std::string> PlexApi::GetServerReportedName()
   {
      auto res = Get(BuildApiPath(API_SERVERS), pimpl_->adminHeaders_);
      if (!IsHttpSuccess(__func__, res))
         return std::nullopt;

      JsonPlexResponse<JsonPlexServerData> serverResponse;
      if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
      {
         LogWarning(
            "{} - JSON Parse Error: {}",
            __func__,
            glz::format_error(ec, res.body));
         return std::nullopt;
      }

      if (serverResponse.response.data.size() == 0)
      {
         LogWarning("{} - No server names reported", __func__);
         return std::nullopt;
      }

      // Return the first name
      return serverResponse.response.data[0].name;
   }

   std::optional<std::string> PlexApi::PlexApiImpl::GetTracearrServerName() const
   {
      return tracearrServerName_;
   }

   std::optional<std::string> PlexApi::GetTracearrServerName() const
   {
      return pimpl_->GetTracearrServerName();
   }

   std::optional<std::string> PlexApi::PlexApiImpl::GetLibraryId(std::string_view libraryName) const
   {
      std::shared_lock lock(dataLock_);
      auto iter = libraries_.find(libraryName);
      return iter == libraries_.end() ? std::nullopt : std::make_optional(iter->second.id);
   }

   std::optional<std::string> PlexApi::GetLibraryId(std::string_view libraryName) const
   {
      return pimpl_->GetLibraryId(libraryName);
   }

   std::optional<PlexSearchResults> PlexApi::GetItemInfoByPathWithToken(std::string_view userToken, const std::filesystem::path& filePath)
   {
      if (userToken.empty())
         return std::nullopt;

      std::string ratingKey;

      // Scope around the lock
      {
         std::shared_lock sharedLock(pimpl_->dataLock_);

         if (auto iter = pimpl_->pathToIdCache_.find(filePath);
             iter == pimpl_->pathToIdCache_.end())
         {
            LogWarning("{} - No rating key found for path {}", __func__, GetTag("path", filePath.generic_string()));
            return std::nullopt;
         }
         else
         {
            ratingKey = iter->second;
         }
      }

      auto headersToUse = pimpl_->headersNoToken_;
      headersToUse.emplace(API_TOKEN_NAME, userToken);

      auto res = Get(BuildApiPath(std::format("{}/{}", API_LIBRARY_DATA, ratingKey)), headersToUse);
      if (!IsHttpSuccess(__func__, res, false)) // Turn off logging it is expected to miss for some paths that are not in the library
         return std::nullopt;

      JsonPlexResponse<JsonPlexMetadataContainer> serverResponse;
      if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
      {
         LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, res.body));
         return std::nullopt;
      }

      PlexSearchResults returnResults;
      returnResults.items.reserve(serverResponse.response.data.size());
      std::ranges::transform(
         serverResponse.response.data,
         std::back_inserter(returnResults.items),
         [&](auto& metadata) { return ToPlexSearchResult(metadata, serverResponse.response.library); });

      return returnResults;
   }

   std::optional<PlexSearchResults> PlexApi::GetItemInfoByPathWithUserName(std::string_view userName, const std::filesystem::path& filePath)
   {
      std::string userToken;

      // Scope around the lock
      {
         std::shared_lock sharedLock(pimpl_->dataLock_);
         auto tokenIter = pimpl_->userTokens_.find(userName);
         if (tokenIter != pimpl_->userTokens_.end())
            userToken = tokenIter->second;
      }

      if (userToken.empty())
      {
         LogWarning("{} - No token found for user {}", __func__, GetTag("userName", userName));
         return std::nullopt;
      }

      return GetItemInfoByPathWithToken(userToken, filePath);
   }

   std::optional<std::filesystem::path> PlexApi::GetItemPath(std::string_view id)
   {
      std::shared_lock sharedLock(pimpl_->dataLock_);
      if (auto iter = pimpl_->idToPathCache_.find(id);
          iter != pimpl_->idToPathCache_.end())
      {
         return iter->second;
      }
      return std::nullopt;
   }

   std::unordered_map<std::string, std::filesystem::path> PlexApi::GetItemsPaths(const std::vector<std::string>& ids)
   {
      std::unordered_map<std::string, std::filesystem::path> results;
      results.reserve(ids.size());

      std::shared_lock sharedLock(pimpl_->dataLock_);
      for (const auto& id : ids)
      {
         if (auto iter = pimpl_->idToPathCache_.find(id);
             iter != pimpl_->idToPathCache_.end())
         {
            results.emplace(id, iter->second);
         }
      }
      return results;
   }

   std::vector<PlexSearchResult> PlexApi::GetItemsByTmdbIds(const std::vector<std::string>& tmdbIds)
   {
      return GetItemsByTmdbIdsImpl(tmdbIds, std::nullopt);
   }

   std::vector<PlexSearchResult> PlexApi::GetItemsByTmdbIds(
      std::string_view libraryName,
      const std::vector<std::string>& tmdbIds)
   {
      return GetItemsByTmdbIdsImpl(tmdbIds, libraryName);
   }

   std::vector<PlexSearchResult> PlexApi::GetItemsByTmdbIdsImpl(
      const std::vector<std::string>& tmdbIds,
      std::optional<std::string_view> libraryName)
   {
      std::vector<std::string> uniqueTmdbIds;
      uniqueTmdbIds.reserve(tmdbIds.size());
      std::unordered_set<std::string> seenTmdbIds;
      seenTmdbIds.reserve(tmdbIds.size());
      for (const auto& id : tmdbIds)
      {
         if (!id.empty() && seenTmdbIds.insert(id).second)
            uniqueTmdbIds.emplace_back(id);
      }
      if (uniqueTmdbIds.empty())
         return {};

      std::vector<PlexLibraryLookup> libraryLookups;
      {
         std::shared_lock sharedLock(pimpl_->dataLock_);
         libraryLookups.reserve(pimpl_->libraries_.size());
         for (const auto& [name, library] : pimpl_->libraries_)
         {
            if (libraryName && *libraryName != name)
               continue;

            if (library.type == "movie")
               libraryLookups.emplace_back(name, library.id, plex_search_movie);
            else if (library.type == "show")
               libraryLookups.emplace_back(name, library.id, plex_search_show);
         }
      }

      std::vector<PlexSearchResult> results;
      std::unordered_set<std::string> seenRatingKeys;
      std::unordered_map<std::string, std::vector<PlexSearchResult>> resultsByTmdbId;
      resultsByTmdbId.reserve(uniqueTmdbIds.size());

      constexpr int32_t pageSize{250};
      const auto pageSizeString = std::to_string(pageSize);
      bool success = true;
      for (const auto& library : libraryLookups)
      {
         const auto searchType = std::to_string(static_cast<int32_t>(library.type));
         int32_t currentIndex{0};
         while (success)
         {
            const auto apiPath = BuildApiParamsPath(
               std::format("{}/{}/all", API_LIBRARIES, library.id),
               {{"type", searchType},
                {"includeGuids", "1"},
                {"X-Plex-Container-Start", std::to_string(currentIndex)},
                {"X-Plex-Container-Size", pageSizeString}});
            auto res = Get(apiPath, pimpl_->adminHeaders_);
            if (!IsHttpSuccess(__func__, res))
            {
               success = false;
               break;
            }

            JsonPlexResponse<JsonPlexMetadataContainer> serverResponse;
            if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
            {
               LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, res.body));
               success = false;
               break;
            }

            if (serverResponse.response.data.empty())
               break;

            for (auto& metadata : serverResponse.response.data)
            {
               auto result = ToPlexSearchResult(metadata, library.name);
               if (!result.tmdbId ||
                   !seenTmdbIds.contains(*result.tmdbId) ||
                   !seenRatingKeys.insert(result.ratingKey).second)
                  continue;

               resultsByTmdbId[*result.tmdbId].emplace_back(std::move(result));
            }

            currentIndex += static_cast<int32_t>(serverResponse.response.data.size());
         }

         if (!success)
            break;
      }

      if (success)
      {
         results.reserve(seenRatingKeys.size());
         for (const auto& tmdbId : uniqueTmdbIds)
         {
            if (auto matchingItems = resultsByTmdbId.find(tmdbId);
                matchingItems != resultsByTmdbId.end())
            {
               for (auto& item : matchingItems->second)
                  results.emplace_back(std::move(item));
            }
         }
      }

      return results;
   }

   void PlexApi::SetLibraryScan(std::string_view libraryId)
   {
      auto apiPath = BuildApiPath(std::format("{}/{}/refresh", API_LIBRARIES, libraryId));
      auto res = Get(apiPath, pimpl_->adminHeaders_);
      IsHttpSuccess(__func__, res);
   }

   void PlexApi::SetLibraryScanPath(std::string_view libraryId, const std::filesystem::path& path)
   {
      auto apiPath = BuildApiParamsPath(std::format("{}/{}/refresh", API_LIBRARIES, libraryId), {{"path", path.generic_string()}});
      auto res = Get(apiPath, pimpl_->adminHeaders_);
      IsHttpSuccess(__func__, res);
   }

   std::string PlexApi::PlexApiImpl::GetCollectionId(std::string_view library, std::string_view collection)
   {
      if (!enableCacheCollections_)
      {
         parent_.LogWarning("{} called but extra cache not enabled", __func__);
         return {};
      }

      std::shared_lock lock(dataLock_);

      auto libIter = libraries_.find(library);
      if (libIter == libraries_.end())
         return {};

      auto iter = collections_.find(libIter->second.id);
      if (iter == collections_.end())
         return {};

      auto subIter = iter->second.find(collection);
      if (subIter == iter->second.end())
         return {};

      return subIter->second;
   }

   std::optional<std::string> PlexApi::PlexApiImpl::GetMachineIdentifier()
   {
      {
         std::shared_lock lock(dataLock_);
         if (machineIdentifier_)
            return machineIdentifier_;
      }
      return std::nullopt;
   }

   bool PlexApi::GetCollectionValid(std::string_view library, std::string_view collection)
   {
      return !pimpl_->GetCollectionId(library, collection).empty();
   }

   std::optional<PlexCollection> PlexApi::GetCollection(std::string_view library, std::string_view collectionName)
   {
      const auto collectionId = pimpl_->GetCollectionId(library, collectionName);
      if (collectionId.empty())
         return std::nullopt;

      auto res = Get(
         BuildApiPath(std::format("{}/{}/children", API_LIBRARY_DATA, collectionId)),
         pimpl_->adminHeaders_);
      if (!IsHttpSuccess(__func__, res))
         return std::nullopt;

      JsonPlexResponse<JsonPlexLibrarySectionResult> serverResponse;
      if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
      {
         LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, res.body));
         return {};
      }

      PlexCollection collection;
      collection.name = collectionName;

      for (auto& data : serverResponse.response.data)
      {
         if (data.media.size() == 0)
            continue;

         auto& item = collection.items.emplace_back();
         item.ratingKey = std::move(data.ratingKey);
         item.title = std::move(data.title);

         for (auto& media : data.media)
         {
            for (auto& part : media.part)
            {
               item.paths.emplace_back(std::move(part.file));
            }
         }
      }

      return collection;
   }

   std::optional<PlexCollectionSort> PlexApi::GetCollectionSort(
      std::string_view library,
      std::string_view collectionName)
   {
      const auto collectionId = pimpl_->GetCollectionId(library, collectionName);
      if (collectionId.empty())
         return std::nullopt;

      const auto apiPath = BuildApiParamsPath(
         std::format("{}/{}", API_LIBRARY_DATA, collectionId),
         {{"includePreferences", "1"}});
      auto res = Get(apiPath, pimpl_->adminHeaders_);
      if (!IsHttpSuccess(__func__, res))
         return std::nullopt;

      JsonPlexResponse<JsonPlexCollectionPreferenceContainer> serverResponse;
      if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
      {
         LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, res.body));
         return std::nullopt;
      }

      if (serverResponse.response.data.empty())
      {
         LogWarning(
            "{} - Collection sort preference was not returned for {}",
            __func__,
            GetTag("collection", collectionName));
         return std::nullopt;
      }

      const auto& collectionSort = serverResponse.response.data.front().collectionSort;
      if (!collectionSort)
      {
         LogWarning(
            "{} - Collection sort preference was not returned for {}",
            __func__,
            GetTag("collection", collectionName));
         return std::nullopt;
      }

      int32_t collectionSortValue{};
      if (const auto* numericValue = std::get_if<int32_t>(&*collectionSort))
      {
         collectionSortValue = *numericValue;
      }
      else
      {
         const auto& stringValue = std::get<std::string>(*collectionSort);
         if (stringValue == "0")
            collectionSortValue = 0;
         else if (stringValue == "1")
            collectionSortValue = 1;
         else if (stringValue == "2")
            collectionSortValue = 2;
         else
         {
            LogWarning(
               "{} - Unknown collection sort preference {} for {}",
               __func__,
               stringValue,
               GetTag("collection", collectionName));
            return std::nullopt;
         }
      }

      switch (collectionSortValue)
      {
      case static_cast<int32_t>(PlexCollectionSort::Release):
         return PlexCollectionSort::Release;
      case static_cast<int32_t>(PlexCollectionSort::Alpha):
         return PlexCollectionSort::Alpha;
      case static_cast<int32_t>(PlexCollectionSort::Custom):
         return PlexCollectionSort::Custom;
      default:
         LogWarning(
            "{} - Unknown collection sort preference {} for {}",
            __func__,
            collectionSortValue,
            GetTag("collection", collectionName));
         return std::nullopt;
      }
   }

   bool PlexApi::CreateCollection(
      std::string_view library,
      std::string_view collectionName,
      const std::vector<std::string>& itemRatingKeys)
   {
      if (itemRatingKeys.empty() ||
          std::ranges::any_of(itemRatingKeys, [](const auto& ratingKey) { return ratingKey.empty(); }))
         return false;

      std::string libraryId;
      std::string libraryType;
      {
         std::shared_lock lock(pimpl_->dataLock_);
         const auto libraryIter = pimpl_->libraries_.find(library);
         if (libraryIter == pimpl_->libraries_.end())
            return false;
         libraryId = libraryIter->second.id;
         libraryType = libraryIter->second.type;
      }

      int32_t searchType{0};
      if (libraryType == "movie")
         searchType = static_cast<int32_t>(plex_search_movie);
      else if (libraryType == "show")
         searchType = static_cast<int32_t>(plex_search_show);
      else
         return false;

      const auto machineIdentifier = pimpl_->GetMachineIdentifier();
      if (!machineIdentifier)
         return false;

      const auto uri = std::format(
         "server://{}/com.plexapp.plugins.library/library/metadata/{}",
         *machineIdentifier,
         BuildCommaSeparatedList(itemRatingKeys));
      const auto searchTypeString = std::to_string(searchType);
      const HttpParams params = {
         {"uri", uri},
         {"type", searchTypeString},
         {"title", collectionName},
         {"smart", "0"},
         {"sectionId", libraryId}};

      const auto apiPath = BuildApiParamsPath("/library/collections", params);
      auto res = Post(apiPath, pimpl_->adminHeaders_);
      if (!IsHttpSuccess(__func__, res))
         return false;

      pimpl_->UpdateCacheCollections(true);
      return true;
   }

   bool PlexApi::SetCollectionSort(
      std::string_view library,
      std::string_view collectionName,
      PlexCollectionSort sort)
   {
      if (sort != PlexCollectionSort::Release &&
          sort != PlexCollectionSort::Alpha &&
          sort != PlexCollectionSort::Custom)
         return false;

      const auto collectionId = pimpl_->GetCollectionId(library, collectionName);
      if (collectionId.empty())
         return false;

      const auto sortValue = std::to_string(static_cast<int32_t>(sort));
      const auto apiPath = BuildApiParamsPath(
         std::format("{}/{}/prefs", API_LIBRARY_DATA, collectionId),
         {{"collectionSort", sortValue}});
      auto res = Put(apiPath, pimpl_->adminHeaders_);
      if (!IsHttpSuccess(__func__, res))
         return false;

      return true;
   }

   bool PlexApi::SetCollectionTitleSort(
      std::string_view library,
      std::string_view collectionName,
      std::string_view titleSort)
   {
      const auto collectionId = pimpl_->GetCollectionId(library, collectionName);
      const auto libraryId = GetLibraryId(library);
      if (collectionId.empty() || !libraryId)
         return false;

      const auto apiPath = BuildApiParamsPath(
         std::format("{}/{}/all", API_LIBRARIES, *libraryId),
         {{"id", collectionId},
          {"type", std::to_string(static_cast<int32_t>(plex_search_collection))},
          {"titleSort.value", titleSort},
          {"titleSort.locked", "1"}});
      auto res = Put(apiPath, pimpl_->adminHeaders_);
      if (!IsHttpSuccess(__func__, res))
         return false;

      return true;
   }

   bool PlexApi::AddCollectionItems(
      std::string_view library,
      std::string_view collectionName,
      const std::vector<std::string>& itemRatingKeys)
   {
      const auto collectionId = pimpl_->GetCollectionId(library, collectionName);
      if (collectionId.empty())
         return false;

      if (itemRatingKeys.empty() ||
          std::ranges::any_of(itemRatingKeys, [](const auto& ratingKey) { return ratingKey.empty(); }))
         return false;

      const auto machineIdentifier = pimpl_->GetMachineIdentifier();
      if (!machineIdentifier)
         return false;

      const auto uri = std::format(
         "server://{}/com.plexapp.plugins.library/library/metadata/{}",
         *machineIdentifier,
         BuildCommaSeparatedList(itemRatingKeys));
      const auto apiPath = BuildApiParamsPath(
         std::format("/library/collections/{}/items", collectionId),
         {{"uri", uri}});
      auto res = Put(apiPath, pimpl_->adminHeaders_);
      if (!IsHttpSuccess(__func__, res))
         return false;

      return true;
   }

   bool PlexApi::RemoveCollectionItems(
      std::string_view library,
      std::string_view collectionName,
      const std::vector<std::string>& itemRatingKeys)
   {
      const auto collectionId = pimpl_->GetCollectionId(library, collectionName);
      if (collectionId.empty())
         return false;

      bool successful{true};
      for (const auto& itemRatingKey : itemRatingKeys)
      {
         if (itemRatingKey.empty())
            continue;

         const auto apiPath = BuildApiPath(
            std::format("/library/collections/{}/items/{}", collectionId, itemRatingKey));
         auto res = Delete(apiPath, pimpl_->adminHeaders_);
         if (!IsHttpSuccess(__func__, res))
            successful = false;
      }

      return successful;
   }

   bool PlexApi::MoveCollectionItem(
      std::string_view library,
      std::string_view collectionName,
      std::string_view itemRatingKey,
      std::optional<std::string_view> afterRatingKey)
   {
      const auto collectionId = pimpl_->GetCollectionId(library, collectionName);
      if (collectionId.empty() || itemRatingKey.empty())
         return false;

      HttpParams params;
      if (afterRatingKey && !afterRatingKey->empty())
         params.emplace_back("after", *afterRatingKey);

      const auto apiPath = BuildApiParamsPath(
         std::format("/library/collections/{}/items/{}/move", collectionId, itemRatingKey),
         params);

      auto res = Put(apiPath, pimpl_->adminHeaders_);
      if (!IsHttpSuccess(__func__, res))
         return false;

      return true;
   }

   bool PlexApi::GetUserTokenAvailable(std::string_view userName) const
   {
      std::shared_lock sharedLock(pimpl_->dataLock_);
      auto iter = pimpl_->userTokens_.find(userName);
      return iter != pimpl_->userTokens_.end();
   }

   bool PlexApi::SetPlayedByUserToken(std::string_view userToken, std::string_view ratingKey, int64_t locationMs)
   {
      if (userToken.empty())
         return false;

      const auto apiPath = BuildApiParamsPath(
         "/:/progress",
         {
            {"identifier", "com.plexapp.plugins.library"},
            {"key", ratingKey},
            {"time", std::format("{}", locationMs)},
            {"state", "stopped"} // 'stopped' commits the time to the database
         });

      auto headersToUse = pimpl_->headersNoToken_;
      headersToUse.emplace(API_TOKEN_NAME, userToken);

      auto res = Get(apiPath, headersToUse);
      if (!IsHttpSuccess(__func__, res))
      {
         auto d = std::chrono::milliseconds(locationMs);
         std::chrono::hh_mm_ss timeSplit{std::chrono::duration_cast<std::chrono::seconds>(d)};
         LogError(
            "{} - Failed to mark {} to play location {}:{}:{}",
            __func__,
            GetTag("ratingKey", ratingKey),
            timeSplit.hours().count(),
            timeSplit.minutes().count(),
            timeSplit.seconds().count());
         return false;
      }

      return true;
   }

   bool PlexApi::SetPlayedByUserName(std::string_view userName, std::string_view ratingKey, int64_t locationMs)
   {
      std::string userToken;

      // Scope around the lock
      {
         std::shared_lock sharedLock(pimpl_->dataLock_);
         auto iter = pimpl_->userTokens_.find(userName);
         if (iter != pimpl_->userTokens_.end())
            userToken = iter->second;
      }

      if (userToken.empty())
      {
         LogWarning("{} - No token found for user {}", __func__, GetTag("userName", userName));
         return false;
      }

      return SetPlayedByUserToken(userToken, ratingKey, locationMs);
   }

   bool PlexApi::SetWatchedByUserToken(std::string_view userToken, std::string_view ratingKey)
   {
      if (userToken.empty())
         return false;

      const auto apiPath = BuildApiParamsPath("/:/scrobble", {{"identifier", "com.plexapp.plugins.library"}, {"key", ratingKey}});

      auto headersToUse = pimpl_->headersNoToken_;
      headersToUse.emplace(API_TOKEN_NAME, userToken);

      auto res = Get(apiPath, headersToUse);
      if (!IsHttpSuccess(__func__, res))
      {
         LogError("{} - Failed to mark {} as watched", __func__, GetTag("ratingKey", ratingKey));
         return false;
      }

      return true;
   }

   bool PlexApi::SetWatchedByUserName(std::string_view userName, std::string_view ratingKey)
   {
      std::string userToken;

      // Scope around the lock
      {
         std::shared_lock sharedLock(pimpl_->dataLock_);
         auto iter = pimpl_->userTokens_.find(userName);
         if (iter != pimpl_->userTokens_.end())
            userToken = iter->second;
      }

      if (userToken.empty())
      {
         LogWarning("{} - No token found for user {}", __func__, GetTag("userName", userName));
         return false;
      }

      return SetWatchedByUserToken(userToken, ratingKey);
   }

   bool PlexApi::PlexApiImpl::CheckHttpSuccess(const warp::Response& res, std::string_view function)
   {
      auto errorStr = plexTvClient_.GetHttpResponseStr(res);
      if (!errorStr)
         return true;

      parent_.LogWarning("{} - HTTP error {}", function, warp::GetTag("error", *errorStr));
      return false;
   }

   void PlexApi::PlexApiImpl::RebuildLibraryMap()
   {
      parent_.LogTrace("Rebuilding Library Map");

      auto res = parent_.Get(parent_.BuildApiPath(API_LIBRARIES), adminHeaders_);
      if (!parent_.IsHttpSuccess(__func__, res))
         return;

      JsonPlexResponse<JsonPlexLibraryResult> serverResponse;
      if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
      {
         parent_.LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, res.body));
         return;
      }

      PlexNameToLibraryMap workingLibraries;
      workingLibraries.reserve(serverResponse.response.libraries.size());
      for (auto& library : serverResponse.response.libraries)
      {
         workingLibraries.emplace(
            std::move(library.title),
            LibraryData{
               .id = std::move(library.id),
               .type = std::move(library.type),
               .agent = std::move(library.agent),
               .contentChangedAt = library.contentChangedAt});
      }

      if (!workingLibraries.empty())
      {
         std::unique_lock lock(dataLock_);
         libraries_ = std::move(workingLibraries);
      }
      else
      {
         parent_.LogWarning("{} - Keeping stale library data due to fetch failures", __func__);
      }
   }

   void PlexApi::PlexApiImpl::RebuildMachineIdentifier()
   {
      parent_.LogTrace("Rebuilding Machine Identifier");

      auto res = parent_.Get(parent_.BuildApiPath("/identity"), adminHeaders_);
      if (!parent_.IsHttpSuccess(__func__, res))
         return;

      JsonPlexResponse<JsonPlexServerIdentity> serverResponse;
      if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
      {
         parent_.LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, res.body));
         return;
      }

      if (serverResponse.response.machineIdentifier.empty())
      {
         parent_.LogWarning("{} - Plex identity did not include machineIdentifier", __func__);
         return;
      }

      std::unique_lock lock(dataLock_);
      machineIdentifier_ = std::move(serverResponse.response.machineIdentifier);
   }

   void PlexApi::PlexApiImpl::RebuildCollectionMap()
   {
      parent_.LogTrace("Rebuilding Collection Map");

      std::vector<std::string> libraryIds;
      {
         std::shared_lock sharedLock(dataLock_);
         libraryIds.reserve(libraries_.size());
         for (const auto& [name, library] : libraries_)
         {
            libraryIds.emplace_back(library.id);
         }
      }

      PlexIdToIdMap workingCollections;
      workingCollections.reserve(libraryIds.size());

      bool success = false;
      for (const auto& id : libraryIds)
      {
         std::string apiPath = parent_.BuildApiParamsPath(std::format("{}/{}/all", API_LIBRARIES, id), {{"type", std::format("{}", static_cast<int>(plex_search_collection))}});

         auto res = parent_.Get(apiPath, adminHeaders_);
         if (!parent_.IsHttpSuccess(__func__, res))
            continue;

         JsonPlexResponse<JsonPlexLibrarySectionResult> serverResponse;
         if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
         {
            parent_.LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, res.body));
            continue;
         }

         // Received valid collections_
         success = true;

         PlexNameToIdMap nameToIdMap;
         nameToIdMap.reserve(serverResponse.response.data.size());

         for (auto& item : serverResponse.response.data)
         {
            nameToIdMap.emplace(std::move(item.title), std::move(item.ratingKey));
         }

         workingCollections.emplace(id, std::move(nameToIdMap));
      }

      if (success)
      {
         std::unique_lock lock(dataLock_);
         collections_ = std::move(workingCollections);
      }
      else
      {
         parent_.LogWarning("{} - Keeping stale collection data due to fetch failures", __func__);
      }
   }

   void PlexApi::PlexApiImpl::RebuildPathMap()
   {
      parent_.LogTrace("Rebuilding Path Map");

      // Returns the type to search for based on the library type.
      // For now ignore libraries that do not use the plex.agents for scanning.
      auto getLibraryType = [](const LibraryData& library) {
         std::optional<PlexSearchTypes> result;
         if (library.agent.starts_with("tv.plex.agents."))
         {
            if (library.type == "movie")
            {
               result = plex_search_movie;
            }
            else if (library.type == "show")
            {
               result = plex_search_episode;
            }
         }
         return result;
      };

      constexpr auto pageSize = 250;
      auto pageSizeStr = std::to_string(pageSize);

      PlexNameToLibraryMap tempLibraryMap;
      {
         std::shared_lock lock(dataLock_);
         tempLibraryMap = libraries_;
      }

      std::vector<std::string> workingPathsSectionIds;
      PlexIdToPathMap workingIdToPathCache;
      PlexPathToIdMap workingPathToIdCache;
      bool allLibrariesSucceeded = true;

      for (auto& library : tempLibraryMap)
      {
         if (!allLibrariesSucceeded)
            break;

         int64_t latestLibraryUpdateTime{0};
         auto libraryType = getLibraryType(library.second);
         if (!libraryType)
            continue;

         workingPathsSectionIds.emplace_back(library.second.id);

         auto libraryTypeStr = std::format("{}", static_cast<int32_t>(*libraryType));
         int32_t currentIndex = 0;
         bool success = true;
         while (success)
         {
            auto apiPath = parent_.BuildApiParamsPath(
               std::format("{}/{}/all", API_LIBRARIES, library.second.id),
               {{"X-Plex-Container-Start", std::format("{}", currentIndex)},
                {"X-Plex-Container-Size", pageSizeStr},
                {"type", libraryTypeStr}});

            // Increment the current index for the next page
            currentIndex += pageSize;

            auto res = parent_.Get(apiPath, adminHeaders_);
            if (!parent_.IsHttpSuccess(__func__, res))
            {
               allLibrariesSucceeded = false;
               break;
            }

            JsonPlexResponse<JsonPlexLibrarySectionResult> serverResponse;
            if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, res.body))
            {
               parent_.LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, res.body));
               allLibrariesSucceeded = false;
               break;
            }

            if (serverResponse.response.data.empty())
            {
               success = false;
               continue;
            }

            for (auto& item : serverResponse.response.data)
            {
               if (item.updatedAt > latestLibraryUpdateTime)
                  latestLibraryUpdateTime = item.updatedAt;

               for (auto& media : item.media)
               {
                  for (auto& part : media.part)
                  {
                     if (!part.file.empty())
                     {
                        // Can't move the rating key because it maybe needed for multiple paths
                        workingIdToPathCache.emplace(item.ratingKey, part.file);
                        workingPathToIdCache.emplace(part.file, item.ratingKey);
                     }
                  }
               }
            }
         }

         library.second.latestUpdateTime = latestLibraryUpdateTime;
      }

      if (allLibrariesSucceeded && !workingIdToPathCache.empty())
      {
         std::unique_lock lock(dataLock_);
         pathsSectionIds_ = std::move(workingPathsSectionIds);
         idToPathCache_ = std::move(workingIdToPathCache);
         pathToIdCache_ = std::move(workingPathToIdCache);

         for (const auto& [tempLibName, tempLibData] : tempLibraryMap)
         {
            if (auto it = libraries_.find(tempLibName);
                it != libraries_.end())
            {
               it->second.latestUpdateTime = tempLibData.latestUpdateTime;
            }
         }
      }
      else
      {
         parent_.LogWarning("{} - Keeping stale collection data due to fetch failures", __func__);
      }
   }

   void PlexApi::PlexApiImpl::RebuildUserTokenMap()
   {
      parent_.LogTrace("Rebuilding User Token Map");

      auto res = plexTvClient_.Get(API_PLEXTV_RESOURCES, plexTvHeaders_);
      if (CheckHttpSuccess(res, __func__) == false)
      {
         parent_.LogWarning("{} - Failed to fetch plex tv resources. Keeping stale data.", __func__);
         return;
      }

      pugi::xml_document doc;
      auto result = doc.load_string(res.body.c_str());

      if (!result)
      {
         parent_.LogWarning("{} - Could not parse {} Plex XML return", __func__, API_PLEXTV_RESOURCES);
         return;
      }

      std::string clientId;
      auto container = doc.child("MediaContainer");
      for (auto device : container.children("Device"))
      {
         std::string currentName = device.attribute("name").value();
         if (currentName == parent_.GetName())
         {
            clientId = device.attribute("clientIdentifier").value();
            break;
         }
      }

      // If the client id was not found return we can't continue
      if (clientId.empty())
      {
         parent_.LogWarning("{} - Failed to fetch client id from plex tv resources. Keeping stale data.", __func__);
         return;
      }

      // Now using the client id fetch the shared server to retrieve user tokens
      auto serverRes = plexTvClient_.Get(std::format("{}/{}/shared_servers", API_PLEXTV_SERVERS, clientId), plexTvHeaders_);
      if (CheckHttpSuccess(serverRes, __func__) == false)
      {
         parent_.LogWarning("{} - Failed to fetch shared servers from plex tv resources. Keeping stale data.", __func__);
         return;
      }

      pugi::xml_document serverDoc;
      auto serverResult = serverDoc.load_string(serverRes.body.c_str());
      if (!serverResult)
      {
         parent_.LogWarning("{} - Could not parse {} Plex XML return", __func__, API_PLEXTV_SERVERS);
         return;
      }

      auto serverContainer = serverDoc.child("MediaContainer");
      PlexNameToIdMap workingUserTokens;
      workingUserTokens.reserve(static_cast<size_t>(serverContainer.attribute("size").as_int()));
      for (auto sharedServer : serverContainer.children("SharedServer"))
      {
         std::string username = sharedServer.attribute("username").value();
         std::string userToken = sharedServer.attribute("accessToken").value();
         if (!username.empty() && !userToken.empty())
         {
            workingUserTokens.emplace(std::move(username), std::move(userToken));
         }
      }

      if (workingUserTokens.empty())
      {
         parent_.LogWarning("{} - Keeping stale User data due to fetch failures", __func__);
         return;
      }

      // Now fetch the admin user to get the token to add to the working set of tokens.
      auto adminUserRes = plexTvClient_.Get(API_PLEXTV_ADMIN_USER, plexTvHeaders_);
      if (CheckHttpSuccess(adminUserRes, __func__) == false)
      {
         parent_.LogWarning("{} - Admin token fetch failed, keeping stale User data", __func__);
         return;
      }

      pugi::xml_document adminUserDoc;
      auto adminUserResult = adminUserDoc.load_string(adminUserRes.body.c_str());
      if (!adminUserResult)
      {
         parent_.LogWarning("{} - Could not parse {} Plex XML return", __func__, API_PLEXTV_ADMIN_USER);
         return;
      }

      // The root node is <user>
      auto userNode = adminUserDoc.child("user");

      // Add the admin user to the working user tokens
      std::string username = userNode.attribute("username").value();
      std::string userToken = userNode.attribute("authToken").value();
      if (username.empty() || userToken.empty())
      {
         parent_.LogWarning("{} - Failed to retrieve admin user name or token", __func__, API_PLEXTV_ADMIN_USER);
         return;
      }

      workingUserTokens.emplace(std::move(username), std::move(userToken));

      std::unique_lock lock(dataLock_);
      userTokens_ = std::move(workingUserTokens);
   }

   void PlexApi::PlexApiImpl::CheckPathMap()
   {
      auto libRes = parent_.Get(parent_.BuildApiPath(API_LIBRARIES), adminHeaders_);
      if (!parent_.IsHttpSuccess(__func__, libRes))
         return;

      JsonPlexResponse<JsonPlexLibraryResult> serverResponse;
      if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(serverResponse, libRes.body))
      {
         parent_.LogWarning("{} - JSON Parse Error: {}", __func__, glz::format_error(ec, libRes.body));
         return;
      }

      struct SearchTarget
      {
         std::string sectionId;
         std::string title;
         int64_t lastKnownUpdate{0};
      };
      std::vector<SearchTarget> targets;

      {
         std::unique_lock lock(dataLock_);
         for (auto& library : serverResponse.response.libraries)
         {
            if (!std::ranges::contains(pathsSectionIds_, library.id))
               continue;

            if (auto it = libraries_.find(library.title);
                it != libraries_.end() && it->second.contentChangedAt != library.contentChangedAt)
            {
               // Update change timestamp immediately so we don't re-trigger this target
               it->second.contentChangedAt = library.contentChangedAt;
               targets.emplace_back(SearchTarget{
                  .sectionId = it->second.id,
                  .title = library.title,
                  .lastKnownUpdate = it->second.latestUpdateTime});
            }
         }
      }

      for (const auto& target : targets)
      {
         auto apiPath = parent_.BuildApiParamsPath(
            std::format("{}/{}/recentlyAdded", API_LIBRARIES, target.sectionId),
            {{"sort", "updatedAt:desc"},
             {"X-Plex-Container-Start", "0"},
             {"X-Plex-Container-Size", "50"}});

         auto recAddRes = parent_.Get(apiPath, adminHeaders_);
         if (!parent_.IsHttpSuccess(__func__, recAddRes))
            continue;

         JsonPlexResponse<JsonPlexLibrarySectionResult> sectionData;
         if (auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(sectionData, recAddRes.body))
            continue;

         if (sectionData.response.data.empty())
            continue;

         // Lock to protect the data updated below
         std::unique_lock lock(dataLock_);

         // Find the library again in case it changed while we were off-thread
         auto libIter = libraries_.find(target.title);
         if (libIter == libraries_.end())
            continue;

         int64_t newLatestUpdateTime = libIter->second.latestUpdateTime;
         for (auto& item : sectionData.response.data)
         {
            if (item.updatedAt <= target.lastKnownUpdate)
               continue;

            newLatestUpdateTime = std::max(newLatestUpdateTime, item.updatedAt);

            for (auto& media : item.media)
            {
               for (auto& part : media.part)
               {
                  if (part.file.empty())
                     continue;

                  parent_.LogTrace("Incremental update: Path:{} -> RatingKey:{}", part.file.string(), item.ratingKey);
                  idToPathCache_.insert_or_assign(item.ratingKey, part.file);
                  pathToIdCache_.insert_or_assign(part.file, item.ratingKey);
               }
            }
         }
         libIter->second.latestUpdateTime = newLatestUpdateTime;
      }
   }

   void PlexApi::PlexApiImpl::UpdateCacheRequired(bool forceRefresh)
   {
      bool refreshLibraries = false;
      bool refreshMachineIdentifier = false;

      // Scope around the lock
      {
         std::shared_lock lock(dataLock_);
         refreshLibraries = forceRefresh || libraries_.empty();
         refreshMachineIdentifier = forceRefresh || !machineIdentifier_;
      }

      if (refreshLibraries)
         RebuildLibraryMap();
      if (refreshMachineIdentifier)
         RebuildMachineIdentifier();
   }

   void PlexApi::PlexApiImpl::UpdateCacheCollections(bool forceRefresh)
   {
      bool refreshCollections = false;

      // Scope around the lock
      {
         std::shared_lock lock(dataLock_);
         refreshCollections = forceRefresh || collections_.empty();
      }

      if (refreshCollections)
         RebuildCollectionMap();
   }

   void PlexApi::PlexApiImpl::UpdateCachePaths(bool forceRefresh)
   {
      bool fullRefreshRequired = false;

      // Scope around the lock
      {
         std::shared_lock lock(dataLock_);
         fullRefreshRequired = forceRefresh || idToPathCache_.empty() || pathToIdCache_.empty();
      }

      if (fullRefreshRequired)
         RebuildPathMap();
      else
         CheckPathMap();
   }

   void PlexApi::PlexApiImpl::UpdateUserTokens(bool forceRefresh)
   {
      bool refreshUserTokens = false;

      // Scope around the lock
      {
         std::shared_lock lock(dataLock_);
         refreshUserTokens = forceRefresh || userTokens_.empty();
      }

      if (refreshUserTokens)
         RebuildUserTokenMap();
   }

   void PlexApi::PlexApiImpl::RefreshCache(bool forceRefresh)
   {
      UpdateCacheRequired(forceRefresh);
      if (enableCacheCollections_)
         UpdateCacheCollections(forceRefresh);
      if (enableCachePaths_)
         UpdateCachePaths(forceRefresh);
      if (enableUserTokens_)
         UpdateUserTokens(forceRefresh);
   }
} // namespace warp
