#pragma once

#include <array>
#include <cstdint>
#include <glaze/json/write.hpp>
#include <list>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace warp
{
   const std::string APPLICATION_JSON{"application/json"};

   inline std::string BuildCommaSeparatedList(const std::vector<std::string>& list)
   {
      if (list.empty())
         return "";

      return std::accumulate(std::next(list.begin()), list.end(), list[0], [](std::string a, const std::string& b) {
         return std::move(a) + "," + b;
      });
   }

   inline std::string GetPercentEncoded(std::string_view src)
   {
      // 1. 256-entry lookup table for "unreserved" characters (RFC 3986)
      static constexpr auto SAFE = []() constexpr {
         std::array<bool, 256> table{};
         for (size_t i = '0'; i <= '9'; ++i)
            table[i] = true;
         for (size_t i = 'A'; i <= 'Z'; ++i)
            table[i] = true;
         for (size_t i = 'a'; i <= 'z'; ++i)
            table[i] = true;
         table[static_cast<unsigned char>('-')] = true;
         table[static_cast<unsigned char>('.')] = true;
         table[static_cast<unsigned char>('_')] = true;
         table[static_cast<unsigned char>('~')] = true;
         return table;
      }();

      static constexpr std::array<char, 16> hex_chars = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

      // 2. Pre-calculate exact size to avoid reallocations
      size_t new_size{0};
      for (char c : src)
      {
         const auto uc = static_cast<unsigned char>(c);
         new_size += SAFE[uc] ? 1 : 3;
      }

      // 3. One single allocation
      std::string result;
      result.reserve(new_size);

      // 4. Encode
      for (char c : src)
      {
         const auto uc = static_cast<unsigned char>(c);
         if (SAFE[uc])
         {
            result.push_back(c);
         }
         else
         {
            result.push_back('%');
            result.push_back(hex_chars[static_cast<size_t>(uc >> 4U)]);
            result.push_back(hex_chars[static_cast<size_t>(uc & 0x0FU)]);
         }
      }

      return result;
   }

   inline std::string ParamsToJson(const std::list<std::pair<std::string_view, std::string_view>> params)
   {
      // Copy into a standard map so Glaze recognizes the structure
      std::map<std::string_view, std::string_view> m;
      for (const auto& [key, value] : params)
      {
         m[key] = value;
      }

      return glz::write_json(m).value_or("{}");
   }
} // namespace warp
