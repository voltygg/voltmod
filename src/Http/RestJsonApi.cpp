#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/StringUtils.hpp>
#include <VoltMod/Http/RestJsonApi.hpp>
#include <utility>

namespace VoltMod::Http
{

using Core::Json;
using Core::StringUtils;

namespace
{
void AppendAuthHeader(std::vector<std::string>& headers, const std::string& apiKey, const std::string& authHeader,
                      const std::string& authScheme)
{
    if (apiKey.empty())
        return;
    const std::string value = authScheme.empty() ? apiKey : authScheme + " " + apiKey;
    headers.push_back(authHeader + ": " + value);
}
}  // namespace

std::optional<HttpRequest> BuildJsonPost(const JsonPostSpec& spec, const std::map<std::string, std::string>& tokens)
{
    if (spec.Url.empty())
        return std::nullopt;

    nlohmann::json body = spec.BodyTemplate.is_null() ? nlohmann::json::object() : spec.BodyTemplate;
    Json::SubstituteTokens(body, tokens);

    std::vector<std::string> headers = {"Content-Type: application/json"};
    AppendAuthHeader(headers, spec.ApiKey, spec.AuthHeader, spec.AuthScheme);

    return HttpRequest{
        .Url = spec.Url,
        .Body = body.dump(),
        .Headers = std::move(headers),
        .TimeoutMs = spec.TimeoutMs,
    };
}

std::optional<HttpRequest> BuildJsonGet(const JsonGetSpec& spec, const std::map<std::string, std::string>& tokens)
{
    if (spec.UrlTemplate.empty())
        return std::nullopt;

    std::vector<std::string> headers;
    AppendAuthHeader(headers, spec.ApiKey, spec.AuthHeader, spec.AuthScheme);

    return HttpRequest{
        .Url = StringUtils::SubstituteTokens(spec.UrlTemplate, tokens),
        .Headers = std::move(headers),
        .TimeoutMs = spec.TimeoutMs,
    };
}

bool IsSuccess(const HttpResult& result)
{
    return result.Ok && result.StatusCode >= 200 && result.StatusCode < 300;
}

std::string ExtractField(const HttpResult& result, std::string_view dotPath, const std::string& valueTemplate)
{
    if (dotPath.empty())
        return {};

    const auto json = nlohmann::json::parse(result.Body, nullptr, /*allow_exceptions=*/false);
    if (!json.is_structured())
        return {};

    const std::string raw = Json::GetStringByPath(json, dotPath);
    if (raw.empty())
        return {};
    return valueTemplate.empty() ? raw : StringUtils::SubstituteTokens(valueTemplate, {{"value", raw}});
}

void Post(HttpClient& client, HttpRequest request, HttpCompletion onComplete)
{
    client.Post(std::move(request.Url), std::move(request.Body), std::move(request.Headers), request.TimeoutMs,
                std::move(onComplete));
}

void Get(HttpClient& client, HttpRequest request, HttpCompletion onComplete)
{
    client.Get(std::move(request.Url), std::move(request.Headers), request.TimeoutMs, std::move(onComplete));
}

}  // namespace VoltMod::Http
