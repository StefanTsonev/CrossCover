#include "HardcoverClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalClock.h>
#ifdef SIMULATOR
#include <ArduinoJsonStringCompat.h>
#endif
#include <Logging.h>
#include <WiFi.h>
#ifdef SIMULATOR
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#else
#include <SecureHttpClient.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "HardcoverCredentialStore.h"

int HardcoverClient::lastHttpCode = 0;
int HardcoverClient::lastTransportError = 0;

namespace {
constexpr char API_URL[] = "https://api.hardcover.app/v1/graphql";
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;
constexpr uint32_t MIN_MAX_ALLOC_FOR_TLS = 24000;
#if 0
// Retained temporarily for reference while the CA-bundle path is verified.
static const char HARDCOVER_GENERATION_YR1[] = R"CERT(-----BEGIN CERTIFICATE-----
MIIE2zCCAsOgAwIBAgIRAKICU/FfJpHAXcHOE7m8yk4wDQYJKoZIhvcNAQELBQAw
LjELMAkGA1UEBhMCVVMxDTALBgNVBAoTBElTUkcxEDAOBgNVBAMTB1Jvb3QgWVIw
HhcNMjUwOTAzMDAwMDAwWhcNMjgwOTAyMjM1OTU5WjAzMQswCQYDVQQGEwJVUzEW
MBQGA1UEChMNTGV0J3MgRW5jcnlwdDEMMAoGA1UEAxMDWVIxMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAoVi8X2xCYgMXvJxNPKp/oF13UMgmPABB07VC
LNDtoXmt9luEZNJSBV10VyT1Pz6LD8Zq1d2gc43WNl1AdRrj4sEnazbOiz0nPpmG
Bp2hui49oZtDIY6wdKeZAi5BbNU20CH6RSBBMLSQ9cXrH8dxdv4PAJ45ssGML68U
SE3BsjC2a6cAN9L5CgXVIQi5tfNiTPoFZZ3S0OlXqLmmtdV95udWAb5b6e/F49Di
CsH0Y00Ag72BVIb1hzynmKe+X0mERBTtsb3BwmpV9ipeBjMLoR/D9cHxHQCWoi5l
TmXwY015J5rGelz1nZjJuxc2kioaX29XJBnhMkP531rSdG5uMwIDAQABo4HuMIHr
MA4GA1UdDwEB/wQEAwIBhjATBgNVHSUEDDAKBggrBgEFBQcDATASBgNVHRMBAf8E
CDAGAQH/AgEAMB0GA1UdDgQWBBQfLzW+RhSCzUCxrnksVXj699Ro+zAfBgNVHSME
GDAWgBTe51tg0CJtQCh9Pw0B/qS1UrRRlDAyBggrBgEFBQcBAQQmMCQwIgYIKwYB
BQUHMAKGFmh0dHA6Ly95ci5pLmxlbmNyLm9yZy8wEwYDVR0gBAwwCjAIBgZngQwB
AgEwJwYDVR0fBCAwHjAcoBqgGIYWaHR0cDovL3lyLmMubGVuY3Iub3JnLzANBgkq
hkiG9w0BAQsFAAOCAgEA0+zvMq3kHig1ddTmmm+RibTr9/RpX7k4buanMMRqbV/y
IvP82zAHN3mvaw+cASuVsdpd0ikjhr4hnhJQLQOzOp2ccKrsdGOAgo0vddeISFAq
EWEV4lmUM3vFF796up+bSgmJ1u6RupDCMxDgF8M3eLvGuj6L0lu3zkQ0KuQLnKxL
tB0oQqn1Idg5CuuGpMvQzk29Pa3D/qHurc0EIM9SxukQuJqq63lxsYyRQFU8yMBO
hq1w5LbfaWNRrz1uklOfI/pYkAb2E2MTZrAMQkBIE2S8Jt1F8gRc96o/xOsrgvSk
a84AisX6xq1lz1Z7jGvrnXc4TMcjxZTjiTaihcYI1JIXZiLtEMSCa5l3cu8YWd6z
dLRQlqRdclVjuQfNHawRJ6GWlkK0QJosivTKwdBw3KxEtzGo8yMHERbsy57gP1UX
HOMcmZYQC0gtyR3SxfenIM/MxC3Ia2Ypab/kQ/CTnlIn2KQ5JUC6NYrGCbhFN9bp
5lKJStEwCUnLpntcrXk5XVDCNv/5RyWpRThkGOV7GetKkQ0qAY8hCzWK6oqnAhDZ
cjlYVdWfqOw3DIOX6EDNBgAqHarRVxyF9QZdOaXSyPJ0ueD2BYJEBgaCGQ8rAaU/
Qc123V5LTXDZW4CcsPBDyhy4v+c8hClAyw/IkJlfBqxB9D+/wvIMHgECZ4ptP6o=
-----END CERTIFICATE-----
)CERT";

static const char HARDCOVER_GENERATION_YR2[] = R"CERT(-----BEGIN CERTIFICATE-----
MIIE2jCCAsKgAwIBAgIQTr0klH4k05SALYSlL9WzGTANBgkqhkiG9w0BAQsFADAu
MQswCQYDVQQGEwJVUzENMAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZUjAe
Fw0yNTA5MDMwMDAwMDBaFw0yODA5MDIyMzU5NTlaMDMxCzAJBgNVBAYTAlVTMRYw
FAYDVQQKEw1MZXQncyBFbmNyeXB0MQwwCgYDVQQDEwNZUjIwggEiMA0GCSqGSIb3
DQEBAQUAA4IBDwAwggEKAoIBAQDZ0LxwBppqh84luqMerV/eeL/fXQ7mLQQv1Lnp
WKZbyvGpx6wh6AfnslAnF6ewTkcHA+gSOoBvm3Dfm06AuGiF+KRut4fAcowqnAQQ
CW98+QPP/eOv/wug7Iyk4NkOxf2I6g2f55T6nJoOTLFcukeRq80JGQEYan+dPFr9
OGUgQK2hGKgNkW87pappsOAuUJcroYhRt5uUis4qaZireiseu32gzDJNBAiKtsvd
6HX4v25bpkRNcS/B/Gtc9kVbUpD+2PLPxdei3Tim55k4tfAEXwD2qyiPTxrTNq6l
N+AMr5g2c1dNqkOTwjxeV6L5lpP1rGiYvLnRaPlOqyZRPW+5AgMBAAGjge4wgesw
DgYDVR0PAQH/BAQDAgGGMBMGA1UdJQQMMAoGCCsGAQUFBwMBMBIGA1UdEwEB/wQI
MAYBAf8CAQAwHQYDVR0OBBYEFEAVLSZ57TIgnt+ach3WMh+BDIEMMB8GA1UdIwQY
MBaAFN7nW2DQIm1AKH0/DQH+pLVStFGUMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEF
BQcwAoYWaHR0cDovL3lyLmkubGVuY3Iub3JnLzATBgNVHSAEDDAKMAgGBmeBDAEC
ATAnBgNVHR8EIDAeMBygGqAYhhZodHRwOi8veXIuYy5sZW5jci5vcmcvMA0GCSqG
SIb3DQEBCwUAA4ICAQB0ZUQWZ9/Yn9COEpo+JfecMnB0h0vwDm/M66IqXqw3LoaL
mx9lZvRTeDIS67PUeI3yCA2W6PKRD0/FE/G57lOmS+Xy5AaaL00ICGOqjNcCaMWW
8o8nevHOd4i4lqgtznE/28QwlcdJyF8yBiWHpnyjhEpmNWJURgOCOg2xpwRMBCsj
MScqYPtOhBeuYQvSwAEeTML2Ukh6uGuX4E14q65Ja8cdjF5bAldnP1eE4FBaAwsZ
G2fOqqrKV03Y85Nw2btedP1AtliQuJZs/Jo/gXxXdc7LrH3McgnpnbTiAncX7yES
hP6kzQejllqMCIt52HOjxDGWafS7Xw+DKwqmH+Eqy8dcbOuag/1AYlQoKNVK3F5q
Hh6tEDiMqQcLIibGKteE6iHo4A/bIScbzrhXUYuism42ZYzmc48FMVIH3qy4L84E
TdAH2gtxw0PAhvRVXp8HP7wfngpzsN/8xOTpeRSbM4+Qbc56G6+Bifmv6sk1ieQb
NA3wJdl4DDUuQSV8hBgx6zoI1ZSGORprDFux7c6rhc77QZMSRrEgomBeklervEve
86ylWmZ3WWHV6RLMi8xNvjd71r4EPIGgY7BZU/VPBkq+uA7Gb6mbJnFgV43uh3xy
LRFgxIAphIukwTGSMZZR+AI+Qnp0BYTWovHXozOf3H8r6hozEoT02JHn0AeTfA==
-----END CERTIFICATE-----
)CERT";

static const char HARDCOVER_GENERATION_YR3[] = R"CERT(-----BEGIN CERTIFICATE-----
MIIE2jCCAsKgAwIBAgIQdv2+nJw5pmI1PsQaDsOT/jANBgkqhkiG9w0BAQsFADAu
MQswCQYDVQQGEwJVUzENMAsGA1UEChMESVNSRzEQMA4GA1UEAxMHUm9vdCBZUjAe
Fw0yNTA5MDMwMDAwMDBaFw0yODA5MDIyMzU5NTlaMDMxCzAJBgNVBAYTAlVTMRYw
FAYDVQQKEw1MZXQncyBFbmNyeXB0MQwwCgYDVQQDEwNZUjMwggEiMA0GCSqGSIb3
DQEBAQUAA4IBDwAwggEKAoIBAQDJS0+QyfrZm8U0nXugJKg+3nraHoxKN9NOsGvH
T9NtcdgThWuj6gizDMqn9VQilyPJ+qKK7rjgBM/XK3ogx61EPbgQY8LiVNn4nsmR
1UFUdalb/cL/mYtXo3lu3qop7k6Ol+pOLzvlBINhl+Mq7l9VxUCM717UpYumNKxG
NRjALGg5H16C93UQlW8KgRpW58fY5Be3cLqv24bbKRFisb7S/HPA967pW6rAO/DF
FKbi35NfHKEG0jIGqxtsbYbK+/0qe2147p9oUO/SNcuaT9poLFWKmcY90UR0hXKy
+qiR6Kv7/e5gCK/BGkjLXM1AfX1CRDwOiIPPsK+RJOIj00ilAgMBAAGjge4wgesw
DgYDVR0PAQH/BAQDAgGGMBMGA1UdJQQMMAoGCCsGAQUFBwMBMBIGA1UdEwEB/wQI
MAYBAf8CAQAwHQYDVR0OBBYEFGllKfkz+Am2QtXh87W1nR9feiJ8MB8GA1UdIwQY
MBaAFN7nW2DQIm1AKH0/DQH+pLVStFGUMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEF
BQcwAoYWaHR0cDovL3lyLmkubGVuY3Iub3JnLzATBgNVHSAEDDAKMAgGBmeBDAEC
ATAnBgNVHR8EIDAeMBygGqAYhhZodHRwOi8veXIuYy5sZW5jci5vcmcvMA0GCSqG
SIb3DQEBCwUAA4ICAQCNj5bxci8knkGgfw3qSv0KbbjZpmKUgZzgYYW6EpiFj4Bx
8CTQ79RCEuitFdGFOvG9xfQdWArQlaO/bsmNcsz0D9wiuwxc9RCo6JC4BKRMcmKE
fRiLzHWwHZfUj0sCAY0yBryOGL80J3sD2C3mjAFwV2mVIeuVKOhQeTcstVceW97+
38AC+juHSq+xu/HuCbX0LgTzMzh8RYy8OtO80IFE2v8qAEDIK2PYoQp27nwftAIC
AtOoGzqHDJqPWeiSTQgt1ndRgGAmV7H0HssX66i8naQFlsiidol4goFYEVMVJArp
g2X5NyumCCX/aSnXKgp8leQ6XoVQz0VI6+k1a0goPUJB7xp67BkCxkwJamDOMpQL
xpdTPI2Z1TO2BuEyzBd8DC/yfm1+rvMCRG3AX44f0ra2ueiM7cdRvzcE+b3c2fun
1n/4xzigiimm8Rzs6Cc0A59mt3XhruOe+zGIXtJgn0wf9XfJhKdKcdEH5VNhFapn
Gc9Tm+fd4n5Qojd7W2CwdBUBgwYIt4Cqxqr2OEf7/k1Xm8eplMQlhwySs/hM0dY6
csOPNIXeJm1YsnJyxUGvKRWjOn+vC9k1SXilnnwhcIs9Pp9e5bckYAnB79VJXN/L
8X8xt/9Qm2xmKKe/xzKA+oRvWKofeFGKU7hcnFbyUwlCu0d+JkgWxQu4x4drcQ==
-----END CERTIFICATE-----
)CERT";

static const char* const HARDCOVER_GENERATION_Y_CERTS[HARDCOVER_YR_CERT_COUNT] = {
    HARDCOVER_GENERATION_YR1,
    HARDCOVER_GENERATION_YR2,
    HARDCOVER_GENERATION_YR3,
};
uint8_t preferredHardcoverCertIndex = 0;
#endif
char lastErrorDetailBuffer[96] = "";

struct UserBookRecord {
  int id = 0;
  int editionId = 0;
  int pages = 0;
  int readId = 0;
};

void setLastErrorDetail(const char* detail) {
  snprintf(lastErrorDetailBuffer, sizeof(lastErrorDetailBuffer), "%s", detail ? detail : "");
}

void setLastErrorDetail(const char* prefix, int httpCode, int transportError) {
  if (transportError < 0) {
    setLastErrorDetail("TLS connection failed");
    return;
  }
  snprintf(lastErrorDetailBuffer, sizeof(lastErrorDetailBuffer), "%s http=%d err=%d", prefix, httpCode, transportError);
}

void copyBodyPreview(const char* body, char* preview, const size_t previewSize) {
  if (!preview || previewSize == 0) return;
  size_t i = 0;
  if (body) {
    for (; i < previewSize - 1 && body[i] != '\0'; i++) {
      const char c = body[i];
      preview[i] = (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
    }
  }
  preview[i] = '\0';
}

bool appendGraphqlStringLiteral(char* out, size_t outSize, size_t& pos, const char* text) {
  if (pos >= outSize) return false;
  out[pos++] = '"';
  if (text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
      const char c = text[i];
      const bool needsEscape = c == '"' || c == '\\';
      const size_t needed = needsEscape ? 2 : 1;
      if (pos + needed >= outSize) return false;
      if (needsEscape) out[pos++] = '\\';
      out[pos++] = c;
    }
  }
  if (pos >= outSize) return false;
  out[pos++] = '"';
  out[pos] = '\0';
  return true;
}

bool hasGraphqlErrors(const JsonDocument& doc) {
  JsonArrayConst errors = doc["errors"].as<JsonArrayConst>();
  return !errors.isNull() && errors.size() > 0;
}

void setGraphqlErrorDetail(const JsonDocument& doc, const char* fallback) {
  const char* message = doc["errors"][0]["message"] | nullptr;
  if (message && message[0] != '\0') {
    setLastErrorDetail(message);
    return;
  }
  setLastErrorDetail(fallback);
}

HardcoverClient::Error postGraphql(const char* query, String& responseBody);

HardcoverClient::Error parseAuth(const char* body) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "");
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Auth JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    setLastErrorDetail("JSON parse failed");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Auth GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL auth error");
    return HardcoverClient::API_ERROR;
  }
  JsonVariantConst meValue = doc["data"]["me"];
  JsonObjectConst me = meValue.is<JsonArrayConst>() ? meValue[0].as<JsonObjectConst>() : meValue.as<JsonObjectConst>();
  const int id = me["id"] | 0;
  const char* username = me["username"] | "";
  if (id <= 0) {
    LOG_ERR("HDC", "Auth response did not include me.id");
    setLastErrorDetail("Auth response missing user");
    return HardcoverClient::AUTH_FAILED;
  }
  HARDCOVER_STORE.setUserInfo(id, username);
  HARDCOVER_STORE.saveToFile();
  return HardcoverClient::OK;
}

HardcoverClient::Error parseOk(const char* body) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "");
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Mutation JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    setLastErrorDetail("JSON parse failed");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Mutation GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL mutation error");
    return HardcoverClient::API_ERROR;
  }
  return HardcoverClient::OK;
}

HardcoverClient::Error parseUserBookRecord(const char* body, UserBookRecord& outRecord) {
  outRecord = {};
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "");
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "User book lookup JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    setLastErrorDetail("JSON parse failed");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "User book lookup GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL user book lookup error");
    return HardcoverClient::API_ERROR;
  }

  JsonObject userBook = doc["data"]["user_books"][0];
  outRecord.id = userBook["id"] | 0;
  outRecord.editionId = userBook["edition_id"] | 0;
  outRecord.pages = userBook["book"]["pages"] | 0;
  JsonArrayConst reads = userBook["user_book_reads"].as<JsonArrayConst>();
  if (!reads.isNull() && reads.size() > 0) {
    outRecord.readId = reads[0]["id"] | 0;
  }
  return HardcoverClient::OK;
}

HardcoverClient::Error parseLibrary(const char* body, std::vector<HardcoverLibraryBook>& outBooks) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "");
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Library JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    setLastErrorDetail("JSON parse failed");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Library GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL library error");
    return HardcoverClient::API_ERROR;
  }

  JsonArray books = doc["data"]["user_books"].as<JsonArray>();
  outBooks.clear();
  outBooks.reserve(books.size());
  for (JsonObject item : books) {
    HardcoverLibraryBook book;
    book.statusId = item["status_id"] | 0;
    book.rating = item["rating"] | 0;
    JsonArray reads = item["user_book_reads"].as<JsonArray>();
    if (!reads.isNull() && reads.size() > 0) {
      book.progressPages = reads[0]["progress_pages"] | 0;
    }
    JsonObject bookObj = item["book"];
    book.bookId = bookObj["id"] | 0;
    book.title = bookObj["title"] | std::string("");
    book.pages = bookObj["pages"] | 0;
    outBooks.push_back(std::move(book));
  }
  return HardcoverClient::OK;
}

int parseBookId(JsonVariantConst value) {
  if (value.is<int>()) return value.as<int>();
  if (value.is<const char*>()) return std::atoi(value.as<const char*>());
  return 0;
}

bool appendSearchIds(char* out, size_t outSize, size_t& pos, JsonArrayConst ids, int& appendedCount) {
  if (pos >= outSize) return false;
  out[pos++] = '[';
  appendedCount = 0;
  for (JsonVariantConst idValue : ids) {
    const int id = parseBookId(idValue);
    if (id <= 0) continue;
    const int written = snprintf(out + pos, outSize - pos, "%s%d", appendedCount > 0 ? "," : "", id);
    if (written <= 0 || static_cast<size_t>(written) >= outSize - pos) return false;
    pos += static_cast<size_t>(written);
    appendedCount++;
  }
  if (pos >= outSize) return false;
  out[pos++] = ']';
  out[pos] = '\0';
  return appendedCount > 0;
}

bool containsIgnoreCase(const char* text, const char* needle) {
  if (!text || !needle || needle[0] == '\0') return false;
  const size_t needleLen = strlen(needle);
  for (size_t i = 0; text[i] != '\0'; i++) {
    size_t j = 0;
    while (j < needleLen && text[i + j] != '\0' &&
           std::tolower(static_cast<unsigned char>(text[i + j])) ==
               std::tolower(static_cast<unsigned char>(needle[j]))) {
      j++;
    }
    if (j == needleLen) return true;
  }
  return false;
}

bool looksLikeSetTitle(const char* title, const bool compilation) {
  if (compilation) return true;
  return containsIgnoreCase(title, " box set") || containsIgnoreCase(title, "boxed set") ||
         containsIgnoreCase(title, "4 books") || containsIgnoreCase(title, " collection") ||
         containsIgnoreCase(title, " bundle");
}

bool normalizeTitleToken(const char c, char* out, size_t outSize, size_t& pos, bool& pendingSpace) {
  if (pos + 1 >= outSize) return false;
  const unsigned char uc = static_cast<unsigned char>(c);
  if (std::isalnum(uc)) {
    if (pendingSpace && pos > 0) {
      out[pos++] = ' ';
      if (pos + 1 >= outSize) return false;
    }
    out[pos++] = static_cast<char>(std::tolower(uc));
    pendingSpace = false;
  } else if (c == ':' || c == '-' || c == '(' || c == '[') {
    return false;
  } else if (pos > 0) {
    pendingSpace = true;
  }
  out[pos] = '\0';
  return true;
}

void normalizeTitle(const char* title, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = '\0';
  if (!title) return;
  size_t pos = 0;
  bool pendingSpace = false;
  for (size_t i = 0; title[i] != '\0'; i++) {
    if (!normalizeTitleToken(title[i], out, outSize, pos, pendingSpace)) {
      break;
    }
  }
  while (pos > 0 && out[pos - 1] == ' ') {
    pos--;
  }
  out[pos] = '\0';
}

bool titlesMatchClosely(const char* expectedTitle, const char* candidateTitle) {
  if (!expectedTitle || expectedTitle[0] == '\0') return true;
  char expected[96];
  char candidate[96];
  normalizeTitle(expectedTitle, expected, sizeof(expected));
  normalizeTitle(candidateTitle, candidate, sizeof(candidate));
  if (expected[0] == '\0' || candidate[0] == '\0') return false;
  const size_t expectedLen = strlen(expected);
  const size_t candidateLen = strlen(candidate);
  const size_t prefixLen = std::min(expectedLen, candidateLen);
  if (prefixLen < 5) return false;
  return strncmp(expected, candidate, prefixLen) == 0 || strstr(candidate, expected) != nullptr ||
         strstr(expected, candidate) != nullptr;
}

HardcoverClient::Error parseBookSearch(const char* body, const char* expectedTitle,
                                       std::vector<HardcoverBookSearchResult>& outBooks, const int limit) {
  outBooks.clear();
  JsonDocument doc;
  JsonDocument filter;
  filter["data"]["search"]["ids"][0] = true;
  filter["errors"][0]["message"] = true;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "", DeserializationOption::Filter(filter));
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Book search JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    if (jsonError == DeserializationError::NoMemory) {
      setLastErrorDetail("Search response too large");
      return HardcoverClient::LOW_MEMORY;
    }
    setLastErrorDetail("Invalid search response");
    return body && body[0] == '\0' ? HardcoverClient::SERVER_ERROR : HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Book search GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL search error");
    return HardcoverClient::API_ERROR;
  }

  JsonArrayConst ids = doc["data"]["search"]["ids"].as<JsonArrayConst>();
  if (ids.isNull() || ids.size() == 0) {
    setLastErrorDetail("No matching book found");
    return HardcoverClient::API_ERROR;
  }

  char query[384];
  size_t pos = 0;
  const int prefixLen = snprintf(query, sizeof(query), "query { books(where: {id: {_in: ");
  if (prefixLen <= 0 || static_cast<size_t>(prefixLen) >= sizeof(query)) return HardcoverClient::LOW_MEMORY;
  pos = static_cast<size_t>(prefixLen);
  int idCount = 0;
  if (!appendSearchIds(query, sizeof(query), pos, ids, idCount)) return HardcoverClient::LOW_MEMORY;
  const int suffixLen =
      snprintf(query + pos, sizeof(query) - pos, "}}, limit: %d) { id title compilation } }", idCount);
  if (suffixLen <= 0 || static_cast<size_t>(suffixLen) >= sizeof(query) - pos) return HardcoverClient::LOW_MEMORY;

  String detailsBody;
  HardcoverClient::Error err = postGraphql(query, detailsBody);
  if (err != HardcoverClient::OK) return err;

  JsonDocument detailsDoc;
  JsonDocument detailsFilter;
  detailsFilter["data"]["books"][0]["id"] = true;
  detailsFilter["data"]["books"][0]["title"] = true;
  detailsFilter["data"]["books"][0]["compilation"] = true;
  detailsFilter["errors"][0]["message"] = true;
  const DeserializationError detailsJsonError =
      deserializeJson(detailsDoc, detailsBody.c_str(), DeserializationOption::Filter(detailsFilter));
  if (detailsJsonError) {
    setLastErrorDetail("Invalid book detail response");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(detailsDoc)) {
    setGraphqlErrorDetail(detailsDoc, "GraphQL book detail error");
    return HardcoverClient::API_ERROR;
  }

  JsonArrayConst books = detailsDoc["data"]["books"].as<JsonArrayConst>();
  for (JsonVariantConst idValue : ids) {
    const int id = parseBookId(idValue);
    for (JsonObjectConst book : books) {
      const int bookId = book["id"] | 0;
      const char* title = book["title"] | "";
      const bool compilation = book["compilation"] | false;
      if (bookId == id && !looksLikeSetTitle(title, compilation)) {
        HardcoverBookSearchResult result;
        result.bookId = id;
        result.title = title;
        const auto insertionPoint =
            std::find_if(outBooks.begin(), outBooks.end(), [&](const HardcoverBookSearchResult& existing) {
              return !titlesMatchClosely(expectedTitle, existing.title.c_str());
            });
        outBooks.insert(insertionPoint, result);
        break;
      }
    }
    if (static_cast<int>(outBooks.size()) >= limit) break;
  }

  if (outBooks.empty()) {
    setLastErrorDetail("No matching book found");
    return HardcoverClient::API_ERROR;
  }
  return HardcoverClient::OK;
}

HardcoverClient::Error parseBookSearch(const char* body, const char* expectedTitle,
                                       HardcoverBookSearchResult& outBook) {
  std::vector<HardcoverBookSearchResult> books;
  const HardcoverClient::Error err = parseBookSearch(body, expectedTitle, books, 1);
  if (err != HardcoverClient::OK) return err;
  outBook = books[0];
  return HardcoverClient::OK;
}

String makeBody(const char* query) {
  JsonDocument doc;
  doc["query"] = query;
  String body;
  serializeJson(doc, body);
  return body;
}

#ifdef SIMULATOR
HardcoverClient::Error postGraphql(const char* query, String& responseBody) {
  HardcoverClient::lastHttpCode = 0;
  HardcoverClient::lastTransportError = 0;
  setLastErrorDetail("");
  if (!HARDCOVER_STORE.hasApiToken()) return HardcoverClient::NO_TOKEN;
  if (WiFi.status() != WL_CONNECTED) {
    setLastErrorDetail("WiFi not connected");
    return HardcoverClient::NETWORK_ERROR;
  }

  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  http.begin(secureClient, API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "CrossCover-X4-Hardcover");
  http.addHeader("Authorization", HARDCOVER_STORE.getApiToken().c_str());

  String body = makeBody(query);
  const int httpCode = http.POST(body);
  HardcoverClient::lastHttpCode = httpCode;
  HardcoverClient::lastTransportError = httpCode < 0 ? httpCode : 0;
  responseBody = http.getString();
  http.end();

  if (httpCode == 200) return HardcoverClient::OK;
  if (httpCode == 401 || httpCode == 403) {
    setLastErrorDetail("Token expired or invalid");
    return HardcoverClient::AUTH_FAILED;
  }
  if (httpCode == 429) {
    setLastErrorDetail("Rate limited");
    return HardcoverClient::RATE_LIMITED;
  }
  if (httpCode < 0) {
    setLastErrorDetail("HTTP request failed", httpCode, HardcoverClient::lastTransportError);
    return HardcoverClient::NETWORK_ERROR;
  }
  return HardcoverClient::SERVER_ERROR;
}
#else
HardcoverClient::Error postGraphql(const char* query, String& responseBody) {
  HardcoverClient::lastHttpCode = 0;
  HardcoverClient::lastTransportError = 0;
  setLastErrorDetail("");
  if (!HARDCOVER_STORE.hasApiToken()) return HardcoverClient::NO_TOKEN;
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("HDC", "WiFi is not connected before Hardcover request (status=%d)", static_cast<int>(WiFi.status()));
    setLastErrorDetail("WiFi not connected");
    return HardcoverClient::NETWORK_ERROR;
  }
  bool clockValid = true;
#if !defined(SIMULATOR)
  const time_t now = time(nullptr);
  struct tm timeInfo{};
  gmtime_r(&now, &timeInfo);
  clockValid = timeInfo.tm_year >= 120;  // 2020 or newer
#endif
  if (!clockValid) {
    LOG_INF("HDC", "System clock is not valid; synchronizing time before TLS");
    if (!halClock.syncSystemTimeFromNTP()) {
      LOG_ERR("HDC", "System clock synchronization failed before TLS");
      setLastErrorDetail("System time sync failed");
      return HardcoverClient::NETWORK_ERROR;
    }
  }
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_HEAP_FOR_TLS || maxAllocHeap < MIN_MAX_ALLOC_FOR_TLS) {
    LOG_ERR("HDC", "Insufficient heap for TLS: free=%u maxAlloc=%u needFree=%u needMaxAlloc=%u",
            static_cast<unsigned>(freeHeap), static_cast<unsigned>(maxAllocHeap),
            static_cast<unsigned>(MIN_HEAP_FOR_TLS), static_cast<unsigned>(MIN_MAX_ALLOC_FOR_TLS));
    setLastErrorDetail("Not enough contiguous memory for secure connection");
    return HardcoverClient::LOW_MEMORY;
  }
  String body = makeBody(query);
  freeink::SecureHttpClient http;
  http.setTimeout(15000);
  http.setInsecure();
  if (!http.begin(API_URL)) {
    LOG_ERR("HDC", "Invalid Hardcover API URL");
    setLastErrorDetail("HTTP init failed");
    return HardcoverClient::NETWORK_ERROR;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.setUserAgent("CrossCover-X4-Hardcover");
  http.addHeader("Authorization", HARDCOVER_STORE.getApiToken());

  LOG_INF("HDC", "POST Hardcover GraphQL bytes=%u heap=%u maxAlloc=%u", static_cast<unsigned>(body.length()),
          static_cast<unsigned>(freeHeap), static_cast<unsigned>(maxAllocHeap));
  const int httpCode = http.POST(std::string(body.c_str()));
  HardcoverClient::lastHttpCode = httpCode;
  HardcoverClient::lastTransportError = httpCode < 0 ? httpCode : 0;
  responseBody = http.getString().c_str();
  http.end();

  LOG_INF("HDC", "Hardcover response http=%d bytes=%u", httpCode, static_cast<unsigned>(responseBody.length()));
  if (httpCode == 200 && responseBody.isEmpty()) {
    setLastErrorDetail("Empty API response");
    return HardcoverClient::SERVER_ERROR;
  }
  if (httpCode == 200) return HardcoverClient::OK;
  if (httpCode == 401 || httpCode == 403) {
    setLastErrorDetail("Token expired or invalid");
    return HardcoverClient::AUTH_FAILED;
  }
  if (httpCode == 429) {
    setLastErrorDetail("Rate limited");
    return HardcoverClient::RATE_LIMITED;
  }
  if (httpCode < 0) {
    LOG_ERR("HDC", "SecureHttpClient TLS/transport connection failed");
    setLastErrorDetail("TLS connection failed");
    return HardcoverClient::NETWORK_ERROR;
  }
  setLastErrorDetail("HTTP server error", httpCode, 0);
  return HardcoverClient::SERVER_ERROR;
}
#endif
}  // namespace

HardcoverClient::Error HardcoverClient::authenticate() {
  String body;
  const Error requestError = postGraphql("query GetMe { me { id username } }", body);
  Error result = requestError == OK ? parseAuth(body.c_str()) : requestError;
  if (result == AUTH_FAILED) {
    // Do not let a failed replacement token continue using the previous
    // account for later private user_books requests.
    HARDCOVER_STORE.setUserInfo(0, "");
    if (!HARDCOVER_STORE.saveToFile()) {
      LOG_ERR("HDC", "Failed to clear cached Hardcover identity after auth failure");
    }
  }
  return result;
}

HardcoverClient::Error findUserBookRecord(const int bookId, UserBookRecord& outRecord) {
  const int userId = HARDCOVER_STORE.getUserId();
  if (userId <= 0) {
    setLastErrorDetail("Authenticate Hardcover first");
    return HardcoverClient::AUTH_FAILED;
  }

  char query[320];
  snprintf(query, sizeof(query),
           "query { user_books(where: {user_id: {_eq: %d}, book_id: {_eq: %d}}, limit: 1) { id edition_id book { "
           "pages } user_book_reads(order_by: {started_at: desc}, limit: 1) { id } } }",
           userId, bookId);
  String body;
  const HardcoverClient::Error err = postGraphql(query, body);
  return err == HardcoverClient::OK ? parseUserBookRecord(body.c_str(), outRecord) : err;
}

HardcoverClient::Error HardcoverClient::upsertBookStatus(int bookId, int statusId) {
  UserBookRecord userBook;
  Error err = findUserBookRecord(bookId, userBook);
  if (err != OK) return err;

  char query[256];
  if (userBook.id > 0) {
    snprintf(query, sizeof(query), "mutation { update_user_book(id: %d, object: {status_id: %d}) { id } }", userBook.id,
             statusId);
  } else {
    snprintf(query, sizeof(query), "mutation { insert_user_book(object: {book_id: %d, status_id: %d}) { id } }", bookId,
             statusId);
  }
  String body;
  err = postGraphql(query, body);
  return err == OK ? parseOk(body.c_str()) : err;
}

HardcoverClient::Error HardcoverClient::updateProgress(int bookId, int progressPercent) {
  UserBookRecord userBook;
  Error err = findUserBookRecord(bookId, userBook);
  if (err != OK) return err;
  if (userBook.id <= 0) {
    err = upsertBookStatus(bookId, 2);
    if (err != OK) return err;
    err = findUserBookRecord(bookId, userBook);
    if (err != OK) return err;
  }
  if (userBook.id <= 0) {
    setLastErrorDetail("Book is not in library");
    return API_ERROR;
  }

  if (progressPercent < 0) progressPercent = 0;
  if (progressPercent > 100) progressPercent = 100;
  int progressPages = progressPercent;
  if (userBook.pages > 0) {
    progressPages = (userBook.pages * progressPercent + 50) / 100;
  }

  char query[320];
  if (userBook.readId > 0) {
    snprintf(query, sizeof(query), "mutation { update_user_book_read(id: %d, object: {progress_pages: %d}) { id } }",
             userBook.readId, progressPages);
  } else if (userBook.editionId > 0) {
    snprintf(query, sizeof(query),
             "mutation { insert_user_book_read(user_book_id: %d, user_book_read: {progress_pages: %d, edition_id: %d}) "
             "{ id } }",
             userBook.id, progressPages, userBook.editionId);
  } else {
    snprintf(query, sizeof(query),
             "mutation { insert_user_book_read(user_book_id: %d, user_book_read: {progress_pages: %d}) { id } }",
             userBook.id, progressPages);
  }
  String body;
  err = postGraphql(query, body);
  return err == OK ? parseOk(body.c_str()) : err;
}

HardcoverClient::Error HardcoverClient::rateBook(int bookId, int rating) {
  UserBookRecord userBook;
  Error err = findUserBookRecord(bookId, userBook);
  if (err != OK) return err;

  char query[256];
  if (userBook.id > 0) {
    snprintf(query, sizeof(query), "mutation { update_user_book(id: %d, object: {rating: %d}) { id } }", userBook.id,
             rating);
  } else {
    snprintf(query, sizeof(query),
             "mutation { insert_user_book(object: {book_id: %d, status_id: 3, rating: %d}) { id } }", bookId, rating);
  }
  String body;
  err = postGraphql(query, body);
  return err == OK ? parseOk(body.c_str()) : err;
}

HardcoverClient::Error HardcoverClient::fetchLibrary(std::vector<HardcoverLibraryBook>& outBooks, int limit) {
  const int userId = HARDCOVER_STORE.getUserId();
  if (userId <= 0) {
    setLastErrorDetail("Authenticate Hardcover first");
    return AUTH_FAILED;
  }

  char query[768];
  snprintf(query, sizeof(query),
           "query { user_books(where: {user_id: {_eq: %d}}, limit: %d, order_by: {date_added: desc}) { status_id "
           "rating user_book_reads(order_by: {started_at: desc}, limit: 1) { progress_pages } book { id title pages } "
           "} }",
           userId, limit);
  String body;
  Error err = postGraphql(query, body);
  return err == OK ? parseLibrary(body.c_str(), outBooks) : err;
}

HardcoverClient::Error HardcoverClient::searchBook(const std::string& searchQuery, HardcoverBookSearchResult& outBook) {
  if (searchQuery.empty()) {
    setLastErrorDetail("Missing search text");
    return API_ERROR;
  }

  char query[384];
  size_t pos = 0;
  const int prefixLen = snprintf(query, sizeof(query), "query { search(query: ");
  if (prefixLen <= 0 || static_cast<size_t>(prefixLen) >= sizeof(query)) return LOW_MEMORY;
  pos = static_cast<size_t>(prefixLen);
  if (!appendGraphqlStringLiteral(query, sizeof(query), pos, searchQuery.c_str())) return LOW_MEMORY;
  const int suffixLen =
      snprintf(query + pos, sizeof(query) - pos, ", query_type: \"Book\", per_page: 5, page: 1) { ids } }");
  if (suffixLen <= 0 || static_cast<size_t>(suffixLen) >= sizeof(query) - pos) return LOW_MEMORY;

  String body;
  Error err = postGraphql(query, body);
  return err == OK ? parseBookSearch(body.c_str(), "", outBook) : err;
}

HardcoverClient::Error HardcoverClient::searchBook(const std::string& title, const std::string& author,
                                                   HardcoverBookSearchResult& outBook) {
  std::vector<HardcoverBookSearchResult> books;
  const Error err = searchBooks(title, author, books, 1);
  if (err != OK) return err;
  outBook = books[0];
  return OK;
}

HardcoverClient::Error HardcoverClient::searchBooks(const std::string& title, const std::string& author,
                                                    std::vector<HardcoverBookSearchResult>& outBooks, const int limit) {
  outBooks.clear();
  outBooks.reserve(static_cast<size_t>(std::max(1, std::min(limit, 5))));
  char searchText[192];
  snprintf(searchText, sizeof(searchText), "%s%s%s", title.c_str(), author.empty() ? "" : " ", author.c_str());
  if (title.empty()) {
    HardcoverBookSearchResult book;
    const Error err = searchBook(searchText, book);
    if (err == OK) outBooks.push_back(book);
    return err;
  }

  char query[384];
  size_t pos = 0;
  const int prefixLen = snprintf(query, sizeof(query), "query { search(query: ");
  if (prefixLen <= 0 || static_cast<size_t>(prefixLen) >= sizeof(query)) return LOW_MEMORY;
  pos = static_cast<size_t>(prefixLen);
  if (!appendGraphqlStringLiteral(query, sizeof(query), pos, searchText)) return LOW_MEMORY;
  const int suffixLen =
      snprintf(query + pos, sizeof(query) - pos, ", query_type: \"Book\", per_page: 5, page: 1) { ids } }");
  if (suffixLen <= 0 || static_cast<size_t>(suffixLen) >= sizeof(query) - pos) return LOW_MEMORY;

  String body;
  Error err = postGraphql(query, body);
  return err == OK ? parseBookSearch(body.c_str(), "", outBooks, limit) : err;
}

void HardcoverClient::shutdownNetwork() {
  const uint32_t freeBefore = ESP.getFreeHeap();
  const uint32_t maxBefore = ESP.getMaxAllocHeap();
  if (WiFi.getMode() == WIFI_OFF) return;

  LOG_INF("HDC", "Shutting down Wi-Fi after Hardcover session: mode=%d free=%u maxAlloc=%u",
          static_cast<int>(WiFi.getMode()), static_cast<unsigned>(freeBefore), static_cast<unsigned>(maxBefore));
  WiFi.disconnect(false);
  delay(30);
  WiFi.mode(WIFI_OFF);
  LOG_INF("HDC", "Hardcover Wi-Fi shutdown complete: free=%u maxAlloc=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

const char* HardcoverClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "OK";
    case NO_TOKEN:
      return "No API token";
    case LOW_MEMORY:
      return "Low memory";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Auth failed";
    case RATE_LIMITED:
      return "Rate limited";
    case SERVER_ERROR:
      return "Server error";
    case JSON_ERROR:
      return "JSON error";
    case API_ERROR:
      return "API error";
  }
  return "Unknown error";
}

const char* HardcoverClient::lastErrorDetail() { return lastErrorDetailBuffer; }
