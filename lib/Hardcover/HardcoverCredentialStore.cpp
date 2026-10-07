#include "HardcoverCredentialStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>
#include <PersistableStore.h>

#include <cstring>
#include <utility>

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
  if (token.size() >= 2 &&
      ((token.front() == '"' && token.back() == '"') || (token.front() == '\'' && token.back() == '\''))) {
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
}  // namespace

bool HardcoverCredentialStore::saveToFile() const {
  JsonDocument doc;
  doc["apiToken_obf"] = obfuscation::obfuscateToBase64(apiToken);
  doc["userId"] = userId;
  doc["username"] = username;

  return PersistableStoreBase::writeDocToFileAtomically(HARDCOVER_FILE_JSON, doc);
}

bool HardcoverCredentialStore::loadFromFile() {
  const std::string backupPath = std::string(HARDCOVER_FILE_JSON) + ".bak";
  if (!Storage.exists(HARDCOVER_FILE_JSON) && !Storage.exists(backupPath.c_str())) {
    LOG_DBG("HDC", "No Hardcover credentials file found");
    return false;
  }

  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(HARDCOVER_FILE_JSON, doc)) return false;

  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  std::string loadedToken = obfuscation::deobfuscateFromBase64(doc["apiToken_obf"] | "", &status);
  const bool legacyToken =
      status == obfuscation::DecodeStatus::INVALID || status == obfuscation::DecodeStatus::EMPTY || loadedToken.empty();
  if (legacyToken) {
    loadedToken = doc["apiToken"] | std::string("");
  }
  loadedToken = normalizeToken(loadedToken);
  const int loadedUserId = doc["userId"] | 0;
  const std::string loadedUsername = doc["username"] | std::string("");

  apiToken = loadedToken;
  userId = loadedUserId;
  username = loadedUsername;
  if (legacyToken && !apiToken.empty() && !saveToFile()) {
    LOG_ERR("HDC", "Could not migrate legacy Hardcover credentials");
  }
  return true;
}

bool HardcoverCredentialStore::importTokenFile() {
  if (!Storage.exists(HARDCOVER_TOKEN_TXT)) return false;

  auto token = makeUniqueNoThrow<char[]>(HARDCOVER_TOKEN_BUFFER_SIZE);
  if (!token) {
    LOG_ERR("HDC", "Could not allocate Hardcover token import buffer");
    return false;
  }

  const size_t bytesRead = Storage.readFileToBuffer(HARDCOVER_TOKEN_TXT, token.get(), HARDCOVER_TOKEN_BUFFER_SIZE,
                                                    HARDCOVER_TOKEN_BUFFER_SIZE - 1);
  if (bytesRead == 0) return false;
  if (bytesRead >= HARDCOVER_TOKEN_BUFFER_SIZE - 1) {
    LOG_ERR("HDC", "Hardcover token file is too large or was truncated");
    return false;
  }

  std::string trimmed = normalizeToken(trimToken(token.get()));
  if (trimmed.empty()) return false;

  if (trimmed != apiToken) {
    std::string previousToken = apiToken;
    apiToken = trimmed;
    LOG_DBG("HDC", "Imported Hardcover API token from text file");
    if (saveToFile()) return true;
    apiToken = std::move(previousToken);
    return false;
  }
  return true;
}

void HardcoverCredentialStore::setApiToken(const std::string& token) { apiToken = token; }

void HardcoverCredentialStore::clearApiToken() {
  const std::string previousToken = apiToken;
  const int previousUserId = userId;
  const std::string previousUsername = username;
  apiToken.clear();
  userId = 0;
  username.clear();
  if (!saveToFile()) {
    apiToken = previousToken;
    userId = previousUserId;
    username = previousUsername;
    LOG_ERR("HDC", "Could not persist cleared Hardcover credentials");
  }
}

void HardcoverCredentialStore::setUserInfo(int id, const std::string& name) {
  userId = id;
  username = name;
}
