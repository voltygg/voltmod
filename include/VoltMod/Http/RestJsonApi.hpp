#pragma once

#include <VoltMod/Http/HttpClient.hpp>
#include <VoltMod/Http/HttpResult.hpp>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::Http
{

/**
 * @brief Config-driven description of a JSON POST endpoint: URL, optional auth header, and a
 * body template whose `{token}` placeholders are substituted per call. Lets server operators
 * wire a plugin to their own backend purely from settings.
 */
struct JsonPostSpec
{
    std::string Url;
    std::string ApiKey; /**< "" sends no auth header. */
    std::string AuthHeader = "Authorization";
    std::string AuthScheme = "Bearer"; /**< "" sends the key verbatim. */
    nlohmann::json BodyTemplate;       /**< `{token}`-substituted per call; null becomes {}. */
    long TimeoutMs = 8000;
};

/**
 * @brief Config-driven description of a JSON GET endpoint: a URL template whose `{token}`
 * placeholders are substituted per call, plus the same optional auth header as @ref JsonPostSpec.
 */
struct JsonGetSpec
{
    std::string UrlTemplate; /**< `{token}`-substituted per call; "" means not configured. */
    std::string ApiKey;      /**< "" sends no auth header. */
    std::string AuthHeader = "Authorization";
    std::string AuthScheme = "Bearer"; /**< "" sends the key verbatim. */
    long TimeoutMs = 8000;
};

/** A ready-to-send request assembled from a @ref JsonPostSpec / @ref JsonGetSpec. */
struct HttpRequest
{
    std::string Url;
    std::string Body;
    std::vector<std::string> Headers;
    long TimeoutMs = 0;
};

/** Assemble the POST from @p spec, substituting @p tokens into the body template and building
 *  the Content-Type/auth headers. Returns nullopt when the spec has no URL configured. */
std::optional<HttpRequest> BuildJsonPost(const JsonPostSpec& spec, const std::map<std::string, std::string>& tokens);

/** Assemble the GET from @p spec, substituting @p tokens into the URL template and building the
 *  auth header. Returns nullopt when the spec has no URL configured. */
std::optional<HttpRequest> BuildJsonGet(const JsonGetSpec& spec, const std::map<std::string, std::string>& tokens);

/** Transport success AND a 2xx status. */
bool IsSuccess(const HttpResult& result);

/** Parse the body as JSON and descend @p dotPath (Json::GetStringByPath). When @p valueTemplate
 *  is non-empty its `{value}` placeholder receives the field. "" on any failure. */
std::string ExtractField(const HttpResult& result, std::string_view dotPath, const std::string& valueTemplate = "");

/** Enqueue a @ref HttpRequest on @p client; `onComplete` runs on the game thread. */
void Post(HttpClient& client, HttpRequest request, HttpCompletion onComplete);

/** Enqueue a @ref HttpRequest as a GET on @p client; `onComplete` runs on the game thread. */
void Get(HttpClient& client, HttpRequest request, HttpCompletion onComplete);

}  // namespace VoltMod::Http
