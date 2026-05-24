#include "HardcoverCredentialStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>

#include <cstring>

HardcoverCredentialStore HardcoverCredentialStore::instance;

namespace {
constexpr char HARDCOVER_FILE_JSON[] = "/.crosspoint/hardcover.json";
constexpr char HARDCOVER_TOKEN_TXT[] = "/.crosspoint/hardcover_token.txt";
constexpr size_t HARDCOVER_TOKEN_BUFFER_SIZE = 2048;

char* trimTokenInPlace(char* raw) {
  if (!raw) return "";

  char* start = raw;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
    start++;
  }

  char* end = start + strlen(start);
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
    end--;
  }
  *end = '\0';
  return start;
}

std::string trimToken(char* raw) { return trimTokenInPlace(raw); }

bool startsWithBearer(const std::string& token) {
  constexpr char prefix[] = "Bearer ";
  if (token.size() < sizeof(prefix) - 1) return false;
  for (size_t i = 0; i < sizeof(prefix) - 1; i++) {
    char c = token[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    char expected = prefix[i];
    if (expected >= 'A' && expected <= 'Z') expected = static_cast<char>(expected - 'A' + 'a');
    if (c != expected) return false;
  }
  return true;
}

std::string stripTokenWrapper(std::string token) {
  if (token.rfind("authorization", 0) == 0 || token.rfind("Authorization", 0) == 0) {
    const size_t equalsPos = token.find('=');
    if (equalsPos != std::string::npos) {
      token = token.substr(equalsPos + 1);
    }
  }

  char* scratch = token.empty() ? nullptr : token.data();
  token = trimToken(scratch);
  if (token.size() >= 2 && ((token.front() == '"' && token.back() == '"') || (token.front() == '\'' && token.back() == '\''))) {
    token = token.substr(1, token.size() - 2);
  }
  scratch = token.empty() ? nullptr : token.data();
  return trimToken(scratch);
}

std::string normalizeToken(const std::string& token) {
  std::string normalized = stripTokenWrapper(token);
  if (normalized.empty() || startsWithBearer(normalized)) return normalized;
  return std::string("Bearer ") + normalized;
}
}

bool HardcoverCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["apiToken_obf"] = obfuscation::obfuscateToBase64(apiToken);
  doc["userId"] = userId;
  doc["username"] = username;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(HARDCOVER_FILE_JSON, json);
}

bool HardcoverCredentialStore::loadFromFile() {
  if (!Storage.exists(HARDCOVER_FILE_JSON)) {
    LOG_DBG("HDC", "No Hardcover credentials file found");
    return false;
  }

  String json = Storage.readFile(HARDCOVER_FILE_JSON);
  if (json.isEmpty()) return false;

  JsonDocument doc;
  auto error = deserializeJson(doc, json.c_str());
  if (error) {
    LOG_ERR("HDC", "Credential JSON parse error: %s", error.c_str());
    return false;
  }

  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  apiToken = obfuscation::deobfuscateFromBase64(doc["apiToken_obf"] | "", &status);
  if (status == obfuscation::DecodeStatus::INVALID || status == obfuscation::DecodeStatus::EMPTY || apiToken.empty()) {
    apiToken = doc["apiToken"] | std::string("");
    if (!apiToken.empty()) saveToFile();
  }
  apiToken = normalizeToken(apiToken);
  userId = doc["userId"] | 0;
  username = doc["username"] | std::string("");
  return true;
}

bool HardcoverCredentialStore::importTokenFile() {
  if (!Storage.exists(HARDCOVER_TOKEN_TXT)) return false;

  auto token = makeUniqueNoThrow<char[]>(HARDCOVER_TOKEN_BUFFER_SIZE);
  if (!token) {
    LOG_ERR("HDC", "Could not allocate Hardcover token import buffer");
    return false;
  }

  const size_t bytesRead =
      Storage.readFileToBuffer(HARDCOVER_TOKEN_TXT, token.get(), HARDCOVER_TOKEN_BUFFER_SIZE, HARDCOVER_TOKEN_BUFFER_SIZE - 1);
  if (bytesRead == 0) return false;
  if (bytesRead >= HARDCOVER_TOKEN_BUFFER_SIZE - 1) {
    LOG_ERR("HDC", "Hardcover token file is too large or was truncated");
    return false;
  }

  std::string trimmed = normalizeToken(trimToken(token.get()));
  if (trimmed.empty()) return false;

  if (trimmed != apiToken) {
    apiToken = trimmed;
    LOG_DBG("HDC", "Imported Hardcover API token from text file");
    return saveToFile();
  }
  return true;
}

void HardcoverCredentialStore::setApiToken(const std::string& token) { apiToken = token; }

void HardcoverCredentialStore::clearApiToken() {
  apiToken.clear();
  userId = 0;
  username.clear();
  saveToFile();
}

void HardcoverCredentialStore::setUserInfo(int id, const std::string& name) {
  userId = id;
  username = name;
}
