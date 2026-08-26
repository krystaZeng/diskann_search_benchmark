#include "baseline/cpp_main/native_reference_capture.hpp"

#include "diskann_pipeline/errors.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace diskann_pipeline::cpp_main {
namespace {

std::string now_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

bool exact_filename(const std::string& value) {
  return !value.empty() && std::filesystem::path(value).filename() == value &&
         value != "." && value != "..";
}

#if defined(__linux__)
void require_same_filesystem(const std::filesystem::path& source_directory,
                             const std::filesystem::path& destination_parent) {
  struct stat source_stat {};
  struct stat destination_stat {};
  if (::stat(source_directory.c_str(), &source_stat) != 0 ||
      ::stat(destination_parent.c_str(), &destination_stat) != 0 ||
      source_stat.st_dev != destination_stat.st_dev) {
    throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                        "native capture requires one filesystem");
  }
}

void copy_exclusive(const std::filesystem::path& source,
                    const std::filesystem::path& destination) {
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "native medoids payload is missing");
  }
  const int descriptor =
      ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
             S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (descriptor < 0) {
    throw PipelineError(ErrorCategory::kInternal,
                        "cannot exclusive-create retained medoids");
  }
  bool success = false;
  try {
    char buffer[64 * 1024];
    while (input) {
      input.read(buffer, sizeof(buffer));
      auto remaining = input.gcount();
      char* cursor = buffer;
      while (remaining > 0) {
        const auto written = ::write(descriptor, cursor,
                                     static_cast<std::size_t>(remaining));
        if (written <= 0) {
          throw PipelineError(ErrorCategory::kInternal,
                              "cannot retain native medoids bytes");
        }
        cursor += written;
        remaining -= written;
      }
    }
    if (!input.eof() || ::fsync(descriptor) != 0) {
      throw PipelineError(ErrorCategory::kInternal,
                          "native medoids retention did not complete");
    }
    success = true;
  } catch (...) {
    ::close(descriptor);
    std::error_code ignored;
    std::filesystem::remove(destination, ignored);
    throw;
  }
  ::close(descriptor);
  if (!success) {
    throw PipelineError(ErrorCategory::kInternal,
                        "native medoids retention did not complete");
  }
}
#endif

}  // namespace

NativeReferenceCaptureResult capture_native_reference(
    const NativeReferenceCaptureRequest& request) {
#if !defined(__linux__)
  static_cast<void>(request);
  throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                      "native Reference capture is Linux-only");
#else
  NativeReferenceCaptureResult result;
  result.started_at = now_utc();
  if (!std::filesystem::is_directory(request.source_directory) ||
      !exact_filename(request.source_graph_name) ||
      !exact_filename(request.source_medoids_name) ||
      request.child_argv.empty() || request.child_argv.front().empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "native capture request is invalid");
  }
  std::filesystem::create_directories(request.captured_graph_path.parent_path());
  std::filesystem::create_directories(
      request.captured_medoids_path.parent_path());
  if (std::filesystem::exists(request.captured_graph_path) ||
      std::filesystem::exists(request.captured_medoids_path)) {
    throw PipelineError(ErrorCategory::kInternal,
                        "native capture output already exists");
  }
  require_same_filesystem(request.source_directory,
                          request.captured_graph_path.parent_path());

  const int inotify = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (inotify < 0) {
    throw PipelineError(ErrorCategory::kInternal,
                        "cannot initialize native capture observer");
  }
  const int watch = ::inotify_add_watch(
      inotify, request.source_directory.c_str(),
      IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO);
  if (watch < 0) {
    ::close(inotify);
    throw PipelineError(ErrorCategory::kInternal,
                        "cannot pre-arm native capture observer");
  }

  std::vector<char*> argv;
  argv.reserve(request.child_argv.size() + 1);
  for (const auto& argument : request.child_argv) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);
  const pid_t child = ::fork();
  if (child == 0) {
    ::execv(argv.front(), argv.data());
    _exit(127);
  }
  if (child < 0) {
    ::inotify_rm_watch(inotify, watch);
    ::close(inotify);
    throw PipelineError(ErrorCategory::kInternal,
                        "cannot launch native Reference child");
  }

  bool child_finished = false;
  int child_status = 0;
  bool graph_created = false;
  try {
    for (;;) {
      struct pollfd descriptor { inotify, POLLIN, 0 };
      static_cast<void>(::poll(&descriptor, 1, child_finished ? 0 : 100));
      if ((descriptor.revents & POLLIN) != 0) {
        alignas(inotify_event) char buffer[4096];
        const auto bytes = ::read(inotify, buffer, sizeof(buffer));
        if (bytes > 0) {
          for (char* cursor = buffer; cursor < buffer + bytes;) {
            const auto* event = reinterpret_cast<inotify_event*>(cursor);
            const std::string name = event->len == 0 ? "" : event->name;
            if (name == request.source_graph_name &&
                (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) != 0) {
              result.exact_graph_event_observed = true;
              if (!graph_created) {
                std::filesystem::create_hard_link(
                    request.source_directory / request.source_graph_name,
                    request.captured_graph_path);
                graph_created = true;
                result.graph_hard_link_created = true;
              }
            }
            cursor += sizeof(inotify_event) + event->len;
          }
        }
      }
      if (!child_finished) {
        const auto wait = ::waitpid(child, &child_status, WNOHANG);
        child_finished = wait == child;
        if (wait < 0) {
          throw PipelineError(ErrorCategory::kInternal,
                              "cannot wait for native Reference child");
        }
      }
      if (child_finished) {
        // One final non-blocking drain above is sufficient because the exact
        // close-write event precedes child exit.
        if ((descriptor.revents & POLLIN) == 0) break;
      }
    }
    result.child_exit_code = WIFEXITED(child_status)
                                 ? WEXITSTATUS(child_status)
                                 : -1;
    if (result.child_exit_code != 0 || !result.exact_graph_event_observed ||
        !result.graph_hard_link_created) {
      throw PipelineError(ErrorCategory::kInternal,
                          "native Reference child or exact capture failed");
    }
    copy_exclusive(request.source_directory / request.source_medoids_name,
                   request.captured_medoids_path);
  } catch (...) {
    if (!child_finished) {
      ::kill(child, SIGTERM);
      static_cast<void>(::waitpid(child, &child_status, 0));
    }
    std::error_code ignored;
    std::filesystem::remove(request.captured_graph_path, ignored);
    std::filesystem::remove(request.captured_medoids_path, ignored);
    ::inotify_rm_watch(inotify, watch);
    ::close(inotify);
    throw;
  }
  ::inotify_rm_watch(inotify, watch);
  ::close(inotify);
  result.ended_at = now_utc();
  return result;
#endif
}

}  // namespace diskann_pipeline::cpp_main
