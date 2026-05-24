#pragma once

#include <string>

class HardcoverCredentialStore {
 private:
  static HardcoverCredentialStore instance;
  std::string apiToken;
  int userId = 0;
  std::string username;

  HardcoverCredentialStore() = default;

 public:
  HardcoverCredentialStore(const HardcoverCredentialStore&) = delete;
  HardcoverCredentialStore& operator=(const HardcoverCredentialStore&) = delete;

  static HardcoverCredentialStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();
  bool importTokenFile();

  void setApiToken(const std::string& token);
  const std::string& getApiToken() const { return apiToken; }
  bool hasApiToken() const { return !apiToken.empty(); }
  void clearApiToken();

  void setUserInfo(int id, const std::string& name);
  int getUserId() const { return userId; }
  const std::string& getUsername() const { return username; }
};

#define HARDCOVER_STORE HardcoverCredentialStore::getInstance()
