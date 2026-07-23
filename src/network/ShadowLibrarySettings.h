#pragma once

#include <PersistableStore.h>

#include <string>

class ShadowLibrarySettings final : public PersistableStore<ShadowLibrarySettings> {
 public:
  static ShadowLibrarySettings& instance() { return getInstance(); }

  const std::string& downloadDirectory() const { return downloadDirectory_; }
  bool setDownloadDirectory(const std::string& path);

 private:
  friend class PersistableStore<ShadowLibrarySettings>;
  ShadowLibrarySettings() = default;

  static const char* getFilePath() { return "/.crosspoint/shadow_library.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  std::string downloadDirectory_ = "/";
};
