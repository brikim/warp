#pragma once

#include <cstdint>
#include <glaze/glaze.hpp>
#include <string>

namespace warp
{
   // File Part Structs
   // {
   struct JsonPlexPart
   {
      std::filesystem::path file;

      // clang-format off
      static constexpr auto value = glz::object(
         "file", &JsonPlexPart::file);
      // clang-format on
   };
   struct JsonPlexMedia
   {
      std::vector<JsonPlexPart> part;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "Part", &JsonPlexMedia::part);
         // clang-format on
      };
   };
   // } File Part Structs

   // Library Section Item Structs
   // {
   struct JsonPlexLibrarySectionItemData
   {
      std::string title;
      std::string ratingKey;
      std::string key;
      int64_t updatedAt;
      std::vector<JsonPlexMedia> media;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "title", &JsonPlexLibrarySectionItemData::title,
            "ratingKey", &JsonPlexLibrarySectionItemData::ratingKey,
            "key", &JsonPlexLibrarySectionItemData::key,
            "updatedAt", &JsonPlexLibrarySectionItemData::updatedAt,
            "Media", &JsonPlexLibrarySectionItemData::media);
         // clang-format on
      };
   };
   struct JsonPlexLibrarySectionResult
   {
      int32_t totalSize;
      std::vector<JsonPlexLibrarySectionItemData> data;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "totalSize", &JsonPlexLibrarySectionResult::totalSize,
            "Metadata", &JsonPlexLibrarySectionResult::data);
         // clang-format on
      };
   };
   // } Library Section Item Structs

   // Library Structs
   // {
   struct JsonPlexLibrary
   {
      std::string title;
      std::string id;
      std::string type;
      std::string agent;
      int64_t contentChangedAt{0};

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "title", &JsonPlexLibrary::title,
            "key", &JsonPlexLibrary::id,
            "type",  &JsonPlexLibrary::type,
            "agent", &JsonPlexLibrary::agent,
            "contentChangedAt", &JsonPlexLibrary::contentChangedAt);
         // clang-format on
      };
   };
   struct JsonPlexLibraryResult
   {
      std::vector<JsonPlexLibrary> libraries;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "Directory", &JsonPlexLibraryResult::libraries);
         // clang-format on
      };
   };
   // } Library Structs

   // Meta data structs
   // {
   struct JsonPlexMetadata
   {
      std::string ratingKey;
      std::string title;
      std::optional<std::string> showTitle;
      uint64_t duration;
      std::optional<int> viewCount;
      std::optional<int64_t> viewOffset;
      std::vector<JsonPlexMedia> media;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "ratingKey", &JsonPlexMetadata::ratingKey,
            "title", &JsonPlexMetadata::title,
            "grandparentTitle", &JsonPlexMetadata::showTitle,
            "duration", &JsonPlexMetadata::duration,
            "viewCount", &JsonPlexMetadata::viewCount,
            "viewOffset", &JsonPlexMetadata::viewOffset,
            "Media", &JsonPlexMetadata::media);
         // clang-format on
      };
   };
   struct JsonPlexMetadataContainer
   {
      std::string library;
      std::vector<JsonPlexMetadata> data;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "librarySectionTitle", &JsonPlexMetadataContainer::library,
            "Metadata", &JsonPlexMetadataContainer::data);
         // clang-format on
      };
   };
   // } Meta data structs

   // Server name structs
   // {
   struct JsonPlexServerName
   {
      std::string name;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "name", &JsonPlexServerName::name);
         // clang-format on
      };
   };
   struct JsonPlexServerData
   {
      std::vector<JsonPlexServerName> data;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "Server", &JsonPlexServerData::data);
         // clang-format on
      };
   };
   // } Server name structs

   template <typename T>
   struct JsonPlexResponse
   {
      T response;

      struct glaze
      {
         // clang-format off
         static constexpr auto value = glz::object(
            "MediaContainer", &JsonPlexResponse::response);
         // clang-format on
      };
   };
} // namespace warp
