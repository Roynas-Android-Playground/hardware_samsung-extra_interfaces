/*
 * Copyright 2021 Soo Hwan Na "Royna"
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#define LOG_TAG "bootlogger"

#include "LoggerInternal.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <poll.h>
#include <spawn.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <utility>
#include <vector>

extern char **environ;

using android::base::GetBoolProperty;
using android::base::WaitForProperty;
using android::base::WriteStringToFile;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

#define MAKE_LOGGER_PROP(prop) "persist.ext.logdump." prop

namespace {

constexpr std::string_view kLogRoot = "/data/debug";
constexpr std::string_view kDevKmsg = "/dev/kmsg";
constexpr std::size_t kRetainedCaptures = 3;
constexpr std::uintmax_t kMaxSourceBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uintmax_t kMinimumFreeBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t kFlushBytes = 64ULL * 1024ULL;
constexpr auto kFlushInterval = 1s;
constexpr auto kSyncInterval = 5s;
constexpr auto kPostBootDrain = 2s;
constexpr std::size_t kMaximumLineBytes = 1024ULL * 1024ULL;

class ScopedFd {
 public:
  ScopedFd() = default;
  explicit ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() { reset(); }
  ScopedFd(const ScopedFd &) = delete;
  ScopedFd &operator=(const ScopedFd &) = delete;
  ScopedFd(ScopedFd &&other) noexcept : fd_(other.release()) {}
  ScopedFd &operator=(ScopedFd &&other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }
  [[nodiscard]] int get() const { return fd_; }
  [[nodiscard]] explicit operator bool() const { return fd_ >= 0; }
  int release() { int value = fd_; fd_ = -1; return value; }
  void reset(int fd = -1) { if (fd_ >= 0) ::close(fd_); fd_ = fd; }
 private:
  int fd_ = -1;
};

bool WriteAll(int fd, std::string_view data) {
  while (!data.empty()) {
    ssize_t written = ::write(fd, data.data(), data.size());
    if (written > 0) { data.remove_prefix(static_cast<std::size_t>(written)); continue; }
    if (written < 0 && errno == EINTR) continue;
    return false;
  }
  return true;
}

class DurableWriter {
 public:
  DurableWriter(fs::path path, std::uintmax_t maxBytes, std::uintmax_t minimumFreeBytes)
      : path_(std::move(path)), maxBytes_(maxBytes), minimumFreeBytes_(minimumFreeBytes),
        lastFlush_(Clock::now()), lastSync_(lastFlush_), lastSpaceCheck_(lastFlush_) {
    fd_.reset(::open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
    if (!fd_) { PLOG(ERROR) << "Failed to open " << path_; accepting_ = false; }
  }
  ~DurableWriter() { finish(); }
  DurableWriter(const DurableWriter &) = delete;
  DurableWriter &operator=(const DurableWriter &) = delete;
  [[nodiscard]] bool valid() const { return static_cast<bool>(fd_); }
  [[nodiscard]] bool accepting() const { return accepting_; }
  bool appendLine(std::string_view line) {
    if (!accepting_ || !fd_) return false;
    auto now = Clock::now();
    if (now - lastSpaceCheck_ >= kFlushInterval) {
      lastSpaceCheck_ = now;
      if (!HasMinimumFreeSpace(path_.parent_path(), minimumFreeBytes_)) {
        stopWithMarker("[bootlogger: capture stopped because free space is low]");
        return false;
      }
    }
    std::uintmax_t incoming = line.size() + 1U;
    if (writtenBytes_ + buffer_.size() + incoming > maxBytes_) {
      stopWithMarker("[bootlogger: source size limit reached; output truncated]");
      return false;
    }
    buffer_.append(line); buffer_.push_back('\n'); maintenance(); return true;
  }
  void maintenance() {
    if (!fd_) return;
    auto now = Clock::now();
    if (!buffer_.empty() && (buffer_.size() >= kFlushBytes || now - lastFlush_ >= kFlushInterval)) flush();
    if (now - lastSync_ >= kSyncInterval) sync();
  }
  void finish() {
    if (!fd_ || finished_) return;
    flush();
    if (::fdatasync(fd_.get()) != 0) PLOG(ERROR) << "fdatasync failed for " << path_;
    finished_ = true;
  }
 private:
  using Clock = std::chrono::steady_clock;
  void flush() {
    if (buffer_.empty() || !fd_) { lastFlush_ = Clock::now(); return; }
    if (!WriteAll(fd_.get(), buffer_)) { PLOG(ERROR) << "Failed to write " << path_; accepting_ = false; buffer_.clear(); return; }
    writtenBytes_ += buffer_.size(); buffer_.clear(); lastFlush_ = Clock::now();
  }
  void sync() {
    flush();
    if (fd_ && ::fdatasync(fd_.get()) != 0) { PLOG(ERROR) << "fdatasync failed for " << path_; accepting_ = false; }
    lastSync_ = Clock::now();
  }
  void stopWithMarker(std::string_view marker) {
    if (!accepting_) return;
    std::uintmax_t remaining = maxBytes_ > writtenBytes_ + buffer_.size() ? maxBytes_ - writtenBytes_ - buffer_.size() : 0;
    if (remaining > 1) {
      auto markerLength = std::min<std::size_t>(marker.size(), static_cast<std::size_t>(remaining - 1));
      buffer_.append(marker.substr(0, markerLength)); buffer_.push_back('\n');
    }
    accepting_ = false; sync();
  }
  fs::path path_; ScopedFd fd_; std::string buffer_;
  std::uintmax_t maxBytes_ = 0, minimumFreeBytes_ = 0, writtenBytes_ = 0;
  bool accepting_ = true, finished_ = false;
  Clock::time_point lastFlush_, lastSync_, lastSpaceCheck_;
};

class StopEvent {
 public:
  StopEvent() : fd_(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) { if (!fd_) PLOG(ERROR) << "eventfd creation failed"; }
  [[nodiscard]] int fd() const { return fd_.get(); }
  [[nodiscard]] bool valid() const { return static_cast<bool>(fd_); }
  void requestStop() {
    if (!fd_) return;
    std::uint64_t value = 1;
    if (::write(fd_.get(), &value, sizeof(value)) < 0 && errno != EAGAIN) PLOG(ERROR) << "Failed to signal logger shutdown";
  }
 private: ScopedFd fd_;
};

void SetNonBlocking(int fd) { int flags = ::fcntl(fd, F_GETFL, 0); if (flags >= 0) (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK); }

class LogcatSource {
 public:
  static constexpr std::string_view NAME = "logcat";
  ~LogcatSource() { stopChild(); }
  bool open() {
    std::array<int,2> pipeFds{};
    if (::pipe2(pipeFds.data(), O_CLOEXEC) != 0) { PLOG(ERROR) << "Failed to create logcat pipe"; return false; }
    ScopedFd readEnd(pipeFds[0]), writeEnd(pipeFds[1]);
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return false;
    (void)posix_spawn_file_actions_adddup2(&actions, writeEnd.get(), STDOUT_FILENO);
    (void)posix_spawn_file_actions_adddup2(&actions, writeEnd.get(), STDERR_FILENO);
    (void)posix_spawn_file_actions_addclose(&actions, readEnd.get());
    (void)posix_spawn_file_actions_addclose(&actions, writeEnd.get());
    char program[] = "logcat"; char *const arguments[] = {program, nullptr};
    int result = ::posix_spawnp(&pid_, program, &actions, nullptr, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) { errno = result; PLOG(ERROR) << "Failed to spawn logcat"; pid_ = -1; return false; }
    writeEnd.reset(); SetNonBlocking(readEnd.get()); fd_ = std::move(readEnd); return true;
  }
  [[nodiscard]] int fd() const { return fd_.get(); }
 private:
  void stopChild() {
    fd_.reset(); if (pid_ <= 0) return;
    (void)::kill(pid_, SIGTERM);
    for (int attempt=0; attempt<20; ++attempt) {
      pid_t result = ::waitpid(pid_, nullptr, WNOHANG);
      if (result == pid_ || (result < 0 && errno == ECHILD)) { pid_ = -1; return; }
      std::this_thread::sleep_for(50ms);
    }
    (void)::kill(pid_, SIGKILL); (void)::waitpid(pid_, nullptr, 0); pid_ = -1;
  }
  ScopedFd fd_; pid_t pid_ = -1;
};

class KernelSource {
 public:
  static constexpr std::string_view NAME = "dmesg";
  bool open() { fd_.reset(::open("/proc/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC)); return static_cast<bool>(fd_); }
  [[nodiscard]] int fd() const { return fd_.get(); }
 private: ScopedFd fd_;
};

#ifdef TESTING
class TestSource {
 public:
  static constexpr std::string_view NAME = "test";
  bool open() { fs::path path=__FILE__; path=path.parent_path()/"testlogfile.log"; fd_.reset(::open(path.c_str(), O_RDONLY|O_CLOEXEC)); return static_cast<bool>(fd_); }
  [[nodiscard]] int fd() const { return fd_.get(); }
 private: ScopedFd fd_;
};
#endif

class AvcCollector {
 public:
  AvcCollector(fs::path directory, std::string sourceName) : directory_(std::move(directory)), sourceName_(std::move(sourceName)) {}
  void consume(std::string_view line) {
    AvcContext context(line); if (!context.isDenied() || context.isUntrustedApp()) return;
    std::string copy(line); if (!uniqueLines_.insert(copy).second) return;
    ensureRawWriter(); if (rawWriter_) rawWriter_->appendLine(copy); contexts_.push_back(std::move(context));
  }
  void maintenance() { if (rawWriter_) rawWriter_->maintenance(); }
  void finish() {
    if (rawWriter_) rawWriter_->finish(); if (contexts_.empty()) return;
    DurableWriter suggestions(directory_/(sourceName_+".suggestions.te"), kMaxSourceBytes, kMinimumFreeBytes);
    if (!suggestions.valid()) return;
    std::string formatted=FormatAllowSuggestions(std::move(contexts_));
    std::size_t begin=0;
    while (begin<formatted.size()) { auto end=formatted.find('\n',begin); if (end==std::string::npos) { suggestions.appendLine(std::string_view(formatted).substr(begin)); break; } suggestions.appendLine(std::string_view(formatted).substr(begin,end-begin)); begin=end+1; }
    suggestions.finish();
  }
 private:
  void ensureRawWriter() { if (!rawWriter_) rawWriter_=std::make_unique<DurableWriter>(directory_/(sourceName_+".avc.txt"),kMaxSourceBytes,kMinimumFreeBytes); }
  fs::path directory_; std::string sourceName_; std::unique_ptr<DurableWriter> rawWriter_; std::unordered_set<std::string> uniqueLines_; AvcContexts contexts_;
};

template <typename Source>
void RunSource(const fs::path &directory, const StopEvent &stopEvent, bool collectAvc) {
  Source source; if (!source.open()) return;
  DurableWriter writer(directory/(std::string(Source::NAME)+".txt"),kMaxSourceBytes,kMinimumFreeBytes); if (!writer.valid()) return;
  std::unique_ptr<AvcCollector> avc; if (collectAvc) avc=std::make_unique<AvcCollector>(directory,std::string(Source::NAME));
  std::string pendingLine; bool dropping=false, eof=false; std::array<char,BUF_SIZE> buffer{};
  auto processLine=[&](std::string line){ if (!line.empty()&&line.back()=='\r') line.pop_back(); writer.appendLine(line); if(avc) avc->consume(line); };
  auto consume=[&](std::string_view bytes){ for(char c:bytes){ if(dropping){ if(c=='\n') dropping=false; continue;} if(c=='\n'){processLine(std::move(pendingLine)); pendingLine.clear(); continue;} pendingLine.push_back(c); if(pendingLine.size()>kMaximumLineBytes){writer.appendLine("[bootlogger: oversized log line omitted]"); pendingLine.clear(); dropping=true;}}};
  while(!eof&&writer.accepting()){
    std::array<pollfd,2> descriptors{{{source.fd(),POLLIN|POLLERR|POLLHUP,0},{stopEvent.fd(),POLLIN,0}}};
    int result=::poll(descriptors.data(),descriptors.size(),250); writer.maintenance(); if(avc) avc->maintenance();
    if(result<0){if(errno==EINTR)continue;break;} if(descriptors[1].revents&POLLIN)break; if(result==0)continue;
    if(descriptors[0].revents&(POLLIN|POLLHUP|POLLERR)){while(true){ssize_t count=::read(source.fd(),buffer.data(),buffer.size()); if(count>0){consume(std::string_view(buffer.data(),static_cast<std::size_t>(count)));continue;} if(count==0){eof=true;break;} if(errno==EINTR)continue; if(errno==EAGAIN||errno==EWOULDBLOCK)break; eof=true;break;}}
  }
  if(!dropping&&!pendingLine.empty())processLine(std::move(pendingLine)); writer.finish(); if(avc)avc->finish();
}

bool WriteTimestamp(const fs::path &directory){fs::path path=directory/"TIMESTAMP";ScopedFd fd(::open(path.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600));if(!fd)return false;std::time_t now=std::time(nullptr);std::tm localTime{};if(::localtime_r(&now,&localTime)==nullptr)return false;std::ostringstream formatted;formatted<<std::put_time(&localTime,"%F %T")<<'\n';return WriteAll(fd.get(),formatted.str())&&::fdatasync(fd.get())==0;}
#ifndef TESTING
void RecordBootTime(){struct sysinfo info{};if(::sysinfo(&info)!=0)return;long minutes=info.uptime/60,seconds=info.uptime%60;std::string message="Boot completed in "+std::to_string(minutes)+"m"+std::to_string(seconds)+"s";LOG(INFO)<<message;(void)WriteStringToFile(message,std::string(kDevKmsg));}
bool KernelAuditEnabled(){KernelConfigType config;if(ReadKernelConfig(config)!=0)return false;auto it=config.find("CONFIG_AUDIT");return it!=config.end()&&it->second==ConfigValue::BUILT_IN;}
#endif
}  // namespace

int main(int argc,char **argv){android::base::InitLogging(argv);::umask(077);fs::path root;std::string captureName;
#ifdef TESTING
(void)argc;root=fs::current_path()/"test-output";captureName="boot";
#else
if(argc!=3)return EXIT_FAILURE;root=fs::path(argv[1]).lexically_normal();captureName=argv[2];if(root!=fs::path(kLogRoot)||!IsSafeCaptureName(captureName))return EXIT_FAILURE;
#endif
fs::path captureDirectory;std::string error;if(!PrepareCaptureDirectory(root,captureName,kRetainedCaptures,&captureDirectory,&error))return EXIT_FAILURE;(void)WriteTimestamp(captureDirectory);StopEvent stopEvent;if(!stopEvent.valid())return EXIT_FAILURE;
#ifdef TESTING
std::thread testThread([&]{RunSource<TestSource>(captureDirectory,stopEvent,true);});testThread.join();return EXIT_SUCCESS;
#else
bool systemLog=std::getenv("LOGGER_MODE_SYSTEM")!=nullptr;bool auditFilterEnabled=GetBoolProperty(MAKE_LOGGER_PROP("audit_filter_enabled"),true);bool kernelAuditEnabled=auditFilterEnabled&&KernelAuditEnabled();std::vector<std::thread> threads;if(!GetBoolProperty("ro.logd.kernel",false))threads.emplace_back([&]{RunSource<KernelSource>(captureDirectory,stopEvent,kernelAuditEnabled);});threads.emplace_back([&]{RunSource<LogcatSource>(captureDirectory,stopEvent,auditFilterEnabled);});if(systemLog)(void)WaitForProperty(MAKE_LOGGER_PROP("enabled"),"false");else{(void)WaitForProperty("sys.boot_completed","1");RecordBootTime();}std::this_thread::sleep_for(kPostBootDrain);stopEvent.requestStop();for(auto &thread:threads)thread.join();return EXIT_SUCCESS;
#endif
}
