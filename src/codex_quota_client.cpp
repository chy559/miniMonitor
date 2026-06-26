#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "codex_quota_client.h"

#include "format_utils.h"
#include "monitor_logic.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

using monitor_logic::extractFirstArrayString;
using monitor_logic::extractJsonObjectForKey;
using monitor_logic::extractJsonString;
using monitor_logic::extractNumberAfter;
using monitor_logic::findJsonCloser;

namespace {
std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return L"";
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return L"";
    }
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

bool readUtf8File(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
    CloseHandle(file);
    return ok;
}

class CodexQuotaClientImpl {
public:
    CodexQuota fetch(bool forceRefresh) {
        return fetchCodexQuota(forceRefresh);
    }

private:
    CodexQuota fetchCodexQuota(bool forceRefresh) {
        CodexQuota quota;
        quota.checked = true;
        quota.status = L"Auth not found";

        wchar_t userProfile[MAX_PATH]{};
        DWORD userProfileSize = GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH);
        if (userProfileSize == 0 || userProfileSize >= MAX_PATH) {
            return quota;
        }

        std::wstring authPath = std::wstring(userProfile) + L"\\.codex\\auth.json";
        std::string authJson;
        if (!readUtf8File(authPath, authJson)) {
            return quota;
        }

        std::string accessToken = extractJsonString(authJson, "access_token");
        std::string accountId = extractJsonString(authJson, "account_id");
        if (accessToken.empty()) {
            quota.status = L"Auth incomplete";
            return quota;
        }

        const std::vector<std::wstring> paths = {
            L"/backend-api/wham/usage",
            L"/backend-api/wham/accounts/check"
        };
        for (const auto& path : paths) {
            DWORD statusCode = 0;
            std::string body = httpGetChatGpt(path, accessToken, accountId, forceRefresh, statusCode);
            if (statusCode >= 200 && statusCode < 300 && !body.empty()) {
                parseQuotaBody(body, quota);
                if (quota.available) {
                    quota.lastUpdated = formatCurrentClock();
                    return quota;
                }
                quota.status = L"Quota unreadable";
            } else if (statusCode == 401 || statusCode == 403) {
                quota.status = L"Login expired";
                return quota;
            } else if (statusCode > 0) {
                quota.status = L"Quota unavailable";
            }
        }
        if (!quota.available && quota.status == L"Auth not found") {
            quota.status = L"Quota unavailable";
        }
        return quota;
    }

    std::string httpGetChatGpt(const std::wstring& path, const std::string& accessToken,
                               const std::string& accountId, bool forceRefresh, DWORD& statusCode) {
        statusCode = 0;
        std::string response;

        HINTERNET session = WinHttpOpen(L"MiniMonitor/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            return response;
        }
        WinHttpSetTimeouts(session, 2000, 2000, 2500, 2500);

        HINTERNET connect = WinHttpConnect(session, L"chatgpt.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) {
            WinHttpCloseHandle(session);
            return response;
        }

        std::wstring requestPath = path;
        if (forceRefresh) {
            requestPath += (requestPath.find(L'?') == std::wstring::npos ? L"?" : L"&");
            requestPath += L"cache_sentinel=" + std::to_wstring(GetTickCount64());
            requestPath += L"&_=" + std::to_wstring(static_cast<unsigned long long>(std::time(nullptr)));
        }

        HINTERNET request = WinHttpOpenRequest(connect, L"GET", requestPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request) {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return response;
        }

        std::string authHeader = "Authorization: Bearer " + accessToken +
                                 "\r\nAccept: application/json"
                                 "\r\nCache-Control: no-cache, no-store, max-age=0"
                                 "\r\nPragma: no-cache"
                                 "\r\nOAI-Product-Sku: CODEX"
                                 "\r\nOAI-Language: en"
                                 "\r\noriginator: Codex Desktop";
        if (!accountId.empty()) {
            authHeader += "\r\nChatGPT-Account-Id: " + accountId;
        }
        authHeader += "\r\n";
        std::wstring headers = utf8ToWide(authHeader);
        BOOL ok = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                  WinHttpReceiveResponse(request, nullptr);

        if (ok) {
            DWORD size = sizeof(statusCode);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0 && response.size() < 1024 * 1024) {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
                    break;
                }
                chunk.resize(read);
                response += chunk;
            }
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return response;
    }

    void parseQuotaBody(const std::string& body, CodexQuota& quota) {
        std::string rateLimit = extractJsonObjectForKey(body, "rate_limit");
        if (rateLimit.empty()) {
            std::string account = selectedCodexAccount(body);
            if (!account.empty()) {
                rateLimit = extractJsonObjectForKey(account, "rate_limit");
            }
        }
        if (rateLimit.empty()) {
            quota.status = L"No quota";
            return;
        }

        QuotaWindow primary = parseLimitWindow(rateLimit, "primary_window");
        QuotaWindow secondary = parseLimitWindow(rateLimit, "secondary_window");

        if (!primary.available && !secondary.available) {
            quota.status = L"Quota unreadable";
            return;
        }

        quota.available = true;
        quota.localAuth = false;
        quota.status = L"Codex quota";
        if (primary.available) {
            quota.firstLabel = primary.label;
            quota.fiveHour = primary.remaining;
            quota.fiveHourReset = primary.reset;
            quota.firstUsage = primary.usage;
            quota.firstProgress = primary.progress;
        }
        if (secondary.available) {
            quota.secondLabel = secondary.label;
            quota.sevenDay = secondary.remaining;
            quota.sevenDayReset = secondary.reset;
            quota.secondUsage = secondary.usage;
            quota.secondProgress = secondary.progress;
        }
    }

    std::string selectedCodexAccount(const std::string& body) {
        std::string selectedId = extractFirstArrayString(body, "account_ordering");
        size_t accountsKey = body.find("\"accounts\"");
        if (accountsKey == std::string::npos) {
            return "";
        }
        size_t colon = body.find(':', accountsKey);
        size_t arrayOpen = colon == std::string::npos ? std::string::npos : body.find('[', colon + 1);
        if (arrayOpen == std::string::npos) {
            return "";
        }
        size_t arrayClose = findJsonCloser(body, arrayOpen, '[', ']');
        if (arrayClose == std::string::npos) {
            return "";
        }

        std::string fallback;
        for (size_t pos = arrayOpen + 1; pos < arrayClose;) {
            size_t objectOpen = body.find('{', pos);
            if (objectOpen == std::string::npos || objectOpen >= arrayClose) {
                break;
            }
            size_t objectClose = findJsonCloser(body, objectOpen, '{', '}');
            if (objectClose == std::string::npos || objectClose > arrayClose) {
                break;
            }

            std::string object = body.substr(objectOpen, objectClose - objectOpen + 1);
            if (fallback.empty() && object.find("\"rate_limit\"") != std::string::npos) {
                fallback = object;
            }
            std::string id = extractJsonString(object, "id");
            if (!selectedId.empty() && id == selectedId) {
                return object;
            }
            pos = objectClose + 1;
        }
        return fallback;
    }

    QuotaWindow parseLimitWindow(const std::string& rateLimit, const std::string& key) {
        QuotaWindow window;
        std::string object = extractJsonObjectForKey(rateLimit, key);
        if (object.empty()) {
            return window;
        }

        double used = 0.0;
        if (!extractNumberAfter(object, 0, {"used_percent"}, used)) {
            return window;
        }

        double windowSeconds = 0.0;
        extractNumberAfter(object, 0, {"limit_window_seconds"}, windowSeconds);
        double resetAt = 0.0;
        extractNumberAfter(object, 0, {"reset_at"}, resetAt);

        const double usedFraction = used > 1.0 ? used / 100.0 : used;
        window.available = true;
        window.usedPercent = used;
        window.windowSeconds = windowSeconds;
        window.resetAt = resetAt;
        window.label = quotaWindowLabel(windowSeconds);
        window.remaining = formatPercent(1.0 - std::clamp(usedFraction, 0.0, 1.0)) + L" left";
        window.reset = formatResetDetail(resetAt);
        window.usage = formatPercent(std::clamp(usedFraction, 0.0, 1.0));
        window.progress = std::clamp(usedFraction, 0.0, 1.0);
        return window;
    }
};


} // namespace

CodexQuota CodexQuotaClient::fetch(bool forceRefresh) {
    CodexQuotaClientImpl client;
    return client.fetch(forceRefresh);
}