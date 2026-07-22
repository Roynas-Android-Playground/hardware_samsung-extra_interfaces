#include "LoggerInternal.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void TestAvcParsing() {
  Expect(ShouldCollectAvcFromSource("dmesg", true), "kernel audit source should be trusted");
  Expect(!ShouldCollectAvcFromSource("logcat", true),
         "application-controlled logcat must not generate suggestions");
  Expect(!ShouldCollectAvcFromSource("dmesg", false),
         "disabled AVC collection must remain disabled");

  AvcContext context(
      R"(type=1400 audit(0.0:1): avc: denied { read open } for name="a file with spaces" scontext=u:r:audioserver:s0 tcontext=u:object_r:vendor_file:s0 tclass=file permissive=0)");
  Expect(context.valid, "valid AVC line should parse");
  Expect(context.isDenied(), "denied AVC should be marked denied");
  Expect(context.operations.count("read") == 1, "read operation missing");
  Expect(context.operations.count("open") == 1, "open operation missing");
  Expect(context.misc_attributes.at("name") == "a file with spaces",
         "quoted attribute was not preserved");

  AvcContext malformed("avc: denied { read for scontext=u:r:init:s0");
  Expect(!malformed.valid, "truncated AVC should be rejected");

  AvcContext malformedSource(
      "avc: denied { read } for scontext=audioserver "
      "tcontext=u:object_r:vendor_file:s0 tclass=file permissive=0");
  Expect(!malformedSource.valid, "malformed source context should be rejected");

  AvcContext prefixedContext(
      "avc: denied { read } for scontext=xu:r:audioserver:s0 "
      "tcontext=u:object_r:vendor_file:s0 tclass=file permissive=0");
  Expect(!prefixedContext.valid, "source context must match the complete token");

  AvcContext malformedMls(
      "avc: denied { read } for scontext=u:r:audioserver:s0:garbage "
      "tcontext=u:object_r:vendor_file:s0 tclass=file permissive=0");
  Expect(!malformedMls.valid, "malformed MLS suffix must be rejected");

  AvcContext malformedPermission(
      "avc: denied { read; } for scontext=u:r:audioserver:s0 "
      "tcontext=u:object_r:vendor_file:s0 tclass=file permissive=0");
  Expect(!malformedPermission.valid, "malformed permission token should be rejected");

  AvcContext malformedClass(
      "avc: denied { read } for scontext=u:r:audioserver:s0 "
      "tcontext=u:object_r:vendor_file:s0 tclass=file; permissive=0");
  Expect(!malformedClass.valid, "malformed class token should be rejected");

  AvcContext untrusted(
      "avc: denied { search } for scontext=u:r:untrusted_app:s0 "
      "tcontext=u:object_r:sysfs:s0 tclass=dir permissive=0");
  Expect(untrusted.valid && untrusted.isUntrustedApp(), "untrusted_app should be recognized");

  AvcContext second(
      "avc: denied { getattr } for scontext=u:r:audioserver:s0 "
      "tcontext=u:object_r:vendor_file:s0 tclass=file permissive=0");
  Expect(context.mergeFrom(second), "matching AVC contexts should merge");
  Expect(second.consumed, "merged AVC context should be consumed");
  Expect(context.operations.count("getattr") == 1, "merged operation missing");
  Expect(context.toAllowRule() == "allow audioserver vendor_file:file { getattr open read };",
         "allow rule formatting is unexpected");
}

void TestKernelConfigParsing() {
  std::string name;
  ConfigValue value = ConfigValue::UNKNOWN;

  Expect(ParseKernelConfigLine("CONFIG_AUDIT=y", &name, &value) && name == "CONFIG_AUDIT" &&
             value == ConfigValue::BUILT_IN,
         "built-in config parse failed");
  Expect(ParseKernelConfigLine("CONFIG_TEST=m", &name, &value) && value == ConfigValue::MODULE,
         "module config parse failed");
  Expect(ParseKernelConfigLine("CONFIG_NAME=\"hello world\"", &name, &value) &&
             value == ConfigValue::STRING,
         "string config parse failed");
  Expect(ParseKernelConfigLine("CONFIG_NUMBER=-42", &name, &value) && value == ConfigValue::INT,
         "integer config parse failed");
  Expect(ParseKernelConfigLine("# CONFIG_UNUSED is not set", &name, &value) &&
             name == "CONFIG_UNUSED" && value == ConfigValue::UNSET,
         "unset config parse failed");
  Expect(!ParseKernelConfigLine("CONFIG_BROKEN=sometimes", &name, &value),
         "unknown config value should fail");
}

void TestCapturePaths() {
  Expect(IsSafeCaptureName("boot"), "boot should be a safe capture name");
  Expect(IsSafeCaptureName("system-1"), "system-1 should be a safe capture name");
  Expect(!IsSafeCaptureName("../boot"), "parent traversal must be rejected");
  Expect(!IsSafeCaptureName("/data/debug"), "absolute capture name must be rejected");
  Expect(!IsSafeCaptureName(".hidden"), "hidden capture names should be rejected");

  const fs::path root = fs::temp_directory_path() / "bootlogger-tests";
  std::error_code ec;
  fs::remove_all(root, ec);

  fs::path current;
  std::string error;
  Expect(PrepareCaptureDirectory(root, "boot", 3, &current, &error),
         "first capture directory preparation failed");
  std::ofstream(current / "generation") << "one";
  Expect(PrepareCaptureDirectory(root, "boot", 3, &current, &error),
         "second capture directory preparation failed");
  std::ofstream(current / "generation") << "two";
  Expect(PrepareCaptureDirectory(root, "boot", 3, &current, &error),
         "third capture directory preparation failed");
  std::ofstream(current / "generation") << "three";
  Expect(PrepareCaptureDirectory(root, "boot", 3, &current, &error),
         "fourth capture directory preparation failed");

  Expect(fs::exists(root / "boot"), "current capture missing");
  Expect(fs::exists(root / "boot.1"), "previous capture missing");
  Expect(fs::exists(root / "boot.2"), "oldest retained capture missing");
  Expect(!fs::exists(root / "boot.3"), "too many captures were retained");

  fs::remove_all(root, ec);
}

}  // namespace

int main() {
  TestAvcParsing();
  TestKernelConfigParsing();
  TestCapturePaths();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "bootlogger tests passed\n";
  return 0;
}
