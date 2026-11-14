#pragma once

#include <memory>
#include <spdlog/formatter.h>
#include <spdlog/pattern_formatter.h>

namespace warp
{
   // Class used to strip the ansii character strings from a log message
   class AnsiiRemoveFormatter : public spdlog::formatter
   {
   public:
      AnsiiRemoveFormatter();
      ~AnsiiRemoveFormatter() override = default;

      void format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dest) override;
      std::unique_ptr<spdlog::formatter> clone() const override;

   private:
      spdlog::pattern_formatter patternFormatter_;
   };
} // namespace warp
