#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
class Epub;
namespace Recap {
// Persists a local handoff only. Upload starts after consent and reader shutdown.
bool prepare(Epub& epub, int currentSpine);
bool hasRequest();
std::string scopeTitle();
bool loadCached(std::string& summary);
using Progress = void (*)(void* context, size_t done, size_t total);
std::string generate(Progress progress = nullptr, void* context = nullptr, uint32_t* retryAfter = nullptr);
}  // namespace Recap
