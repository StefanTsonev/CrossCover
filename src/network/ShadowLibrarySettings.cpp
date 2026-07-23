#include "ShadowLibrarySettings.h"

bool ShadowLibrarySettings::setDownloadDirectory(const std::string& path) {
  if (path.empty() || path.front() != '/') return false;
  downloadDirectory_ = path;
  if (downloadDirectory_.size() > 1 && downloadDirectory_.back() == '/') downloadDirectory_.pop_back();
  return saveToFile();
}

void ShadowLibrarySettings::toJson(JsonDocument& doc) const { doc["download_directory"] = downloadDirectory_; }

bool ShadowLibrarySettings::fromJson(JsonVariantConst doc) {
  const char* path = doc["download_directory"] | "/";
  if (path[0] != '/') return false;
  downloadDirectory_ = path;
  if (downloadDirectory_.size() > 1 && downloadDirectory_.back() == '/') downloadDirectory_.pop_back();
  return true;
}
