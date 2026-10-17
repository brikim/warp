#pragma once

#include "warp/utils.h"

#include "warp/http/http-client.h"

#include <spdlog/sinks/base_sink.h>

#include <format>
#include <mutex>
#include <string>

namespace warp
{
   template<typename Mutex>
   class LogGotifySink : public spdlog::sinks::base_sink<Mutex>
   {
   public:
      explicit LogGotifySink(const GotifyLoggingConfig& config)
         : client_(config.url)
         , title_(config.message_title)
         , priority_{std::format("{}", config.priority)}
         , headers_{{"X-Gotify-Key", config.key}}
      {}

   protected:
      void sink_it_(const spdlog::details::log_msg& msg) override
      {
         spdlog::memory_buf_t formatted;
         spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);

         auto message = StripAnsiiCharacters(fmt::to_string(formatted));

         size_t pos = 0u;
         while ((pos = message.find('"', pos)) != std::string::npos)
         {
            message.replace(pos, 1, "\\\"");
            pos += 2;
         }

         HttpParams params{
            {"title", title_},
            {"message", message},
            {"priority", priority_}
         };
         auto res = client_.Post("/message", headers_, params);
      }

      void flush_() override
      {}

   private:
      HttpClient client_;
      std::string title_;
      std::string priority_;
      Headers headers_;
   };

   using gotify_sink_mt = LogGotifySink<std::mutex>;
}