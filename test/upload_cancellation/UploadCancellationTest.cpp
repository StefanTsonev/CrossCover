// Handler bodies are extracted from production; network and SD I/O are deterministic fakes.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define CROSSINK_SCALABLE_FONTS 1

class String : public std::string {
 public:
  using std::string::string;
  using std::string::operator=;
  bool isEmpty() const { return empty(); }
  bool endsWith(const char* suffix) const {
    return size() >= strlen(suffix) && compare(size() - strlen(suffix), strlen(suffix), suffix) == 0;
  }
};
struct HalFile {
  bool opened = true;
  std::vector<uint8_t> bytes;
  explicit operator bool() const { return opened; }
  bool isOpen() const { return opened; }
  void close() { opened = false; }
  size_t write(const uint8_t* data, size_t size) {
    assert(opened);
    bytes.insert(bytes.end(), data, data + size);
    return size;
  }
};
struct StorageMock {
  std::vector<std::string> removed;
  void remove(const char* path) { removed.emplace_back(path); }
} Storage;
struct FontSystem {
  void markRegistryDirtyForPath(const char*) {}
} sdFontSystem;
struct ImageFolderIndex {
  static void invalidateForPath(const char*) {}
};
unsigned cacheClears = 0;
void clearBookCachePreservingUserState(const char*) { ++cacheClears; }
unsigned long millis() { return 100; }
unsigned long uploadStartTime = 0;
struct FontInstaller {
  static unsigned validations;
  static bool validateCpfontFile(const char*) {
    ++validations;
    return true;
  }
};
unsigned FontInstaller::validations = 0;
enum { UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
  int status = UPLOAD_FILE_WRITE;
  const uint8_t* buf = nullptr;
  size_t currentSize = 0;
};
struct Client {
  bool connected = true;
  void stop() { connected = false; }
};
struct Server {
  HTTPUpload body;
  Client socket;
  HTTPUpload& upload() { return body; }
  Client& client() { return socket; }
};
class CrossPointWebServer {
 public:
  struct UploadState {
    HalFile file;
    String path = "/books";
    String fileName = "test.epub";
    String error;
    bool success = false;
    size_t size = 0;
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer = std::vector<uint8_t>(UPLOAD_BUFFER_SIZE);
    size_t bufferPos = 0;
  } upload;
  struct FontUploadState {
    HalFile file;
    std::string filePath = "/fonts/Test/Regular.cpfont";
    bool valid = true;
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer = std::vector<uint8_t>(BUFFER_SIZE);
    size_t bufferPos = 0;
  } fontUpload;
  using UploadCancelCheck = bool (*)(void*);
  UploadCancelCheck uploadCancelCheck = nullptr;
  void* uploadCancelContext = nullptr;
  std::unique_ptr<Server> server = std::make_unique<Server>();
  bool dropUploadIfCancelled() const;
  void abortUpload(UploadState&) const;
  void abortFontUpload();
  void handleUpload(UploadState&) const;
  void handleFontUploadData();
};
struct MappedInputManager {
  enum class Button { Back };
  unsigned polls = 0;
  bool held = false;
  void update() { ++polls; }
  bool isPressed(Button) { return held; }
};
struct CrossPointWebServerActivity {
  MappedInputManager mappedInput;
  bool leaveRequested = false;
  bool exitEvent = false;
  bool exitRequested() const { return exitEvent; }
  bool checkUploadCancellation();
};
#include "UploadHandlers.inc"

void send(CrossPointWebServer& server, bool font, int status, size_t bytes = 0) {
  static uint8_t body[8192] = {};
  assert(bytes <= sizeof(body));
  server.server->body = {status, body, bytes};
  if (font)
    server.handleFontUploadData();
  else
    server.handleUpload(server.upload);
}

int main() {
  for (bool font : {false, true}) {
    // Normal multi-chunk uploads still finish with every byte and no deletion.
    Storage.removed.clear();
    CrossPointWebServer server;
    send(server, font, UPLOAD_FILE_WRITE, 6000);
    send(server, font, UPLOAD_FILE_WRITE, 77);
    send(server, font, UPLOAD_FILE_END);
    assert(Storage.removed.empty());
    assert(font ? server.fontUpload.valid : server.upload.success);
    assert((font ? server.fontUpload.file.bytes.size() : server.upload.file.bytes.size()) == 6077);
    assert(!(font ? server.fontUpload.file.isOpen() : server.upload.file.isOpen()));

    for (int cancelStatus : {UPLOAD_FILE_WRITE, UPLOAD_FILE_END}) {
      Storage.removed.clear();
      cacheClears = FontInstaller::validations = 0;
      CrossPointWebServer cancelled;
      CrossPointWebServerActivity activity;
      cancelled.uploadCancelCheck = [](void* ctx) {
        return static_cast<CrossPointWebServerActivity*>(ctx)->checkUploadCancellation();
      };
      cancelled.uploadCancelContext = &activity;
      send(cancelled, font, UPLOAD_FILE_WRITE, 5000);  // disk data plus unflushed tail
      activity.mappedInput.held = true;
      send(cancelled, font, cancelStatus, 10);
      assert(activity.leaveRequested && !cancelled.server->socket.connected);
      assert(!(font ? cancelled.fontUpload.file.isOpen() : cancelled.upload.file.isOpen()));
      assert((font ? cancelled.fontUpload.bufferPos : cancelled.upload.bufferPos) == 0);
      assert(!(font ? cancelled.fontUpload.valid : cancelled.upload.success));
      assert(Storage.removed.front() == (font ? "/fonts/Test/Regular.cpfont" : "/books/test.epub"));
      const auto pollCount = activity.mappedInput.polls;
      activity.mappedInput.held = false;
      // Arduino may still dispatch END or ABORTED after the cancellation callback.
      send(cancelled, font, UPLOAD_FILE_END);
      send(cancelled, font, UPLOAD_FILE_ABORTED);
      assert(activity.mappedInput.polls == pollCount);  // latched cancellation does not poll again
      assert(!(font ? cancelled.fontUpload.valid : cancelled.upload.success));
      assert(cacheClears == 0 && FontInstaller::validations == 0);
    }
    // Unrequested transport abort still cleans up.
    CrossPointWebServer aborted;
    send(aborted, font, UPLOAD_FILE_WRITE, 80);
    send(aborted, font, UPLOAD_FILE_ABORTED);
    assert(!(font ? aborted.fontUpload.file.isOpen() : aborted.upload.file.isOpen()));
  }
  // Touch-header/Home/back-release exit events use the same latch as held Back.
  CrossPointWebServerActivity touched;
  touched.exitEvent = true;
  assert(touched.checkUploadCancellation());
  touched.exitEvent = false;
  assert(touched.checkUploadCancellation() && touched.mappedInput.polls == 1);
}
