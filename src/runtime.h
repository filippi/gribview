#pragma once

#include <filesystem>
#include <memory>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static std::filesystem::path ExecutablePath() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> path(size);
  if (_NSGetExecutablePath(path.data(), &size) == 0)
    return std::filesystem::weakly_canonical(path.data());
#elif defined(_WIN32)
  std::vector<wchar_t> path(32768);
  DWORD size = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
  if (size && size < path.size())
    return std::filesystem::path(std::wstring(path.data(), size));
#else
  std::error_code error;
  auto path = std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error)
    return path;
#endif
  return {};
}

static std::filesystem::path ResourcePath(const std::string &name) {
  auto base = ExecutablePath().parent_path();
  for (const auto &root : {base / "../Resources", base / "../share/gribview"}) {
    auto candidate = (root / name).lexically_normal();
    if (std::filesystem::exists(candidate))
      return candidate;
  }
  return {};
}

static int CheckGrib(const std::string &path) {
  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(path.c_str(), "rb"), fclose);
  if (!file) {
    std::cerr << "Cannot open " << path << "\n";
    return 1;
  }
  size_t count = 0;
  int error = 0;
  while (true) {
    std::unique_ptr<codes_handle, decltype(&codes_handle_delete)> handle(
        codes_handle_new_from_file(nullptr, file.get(), PRODUCT_GRIB, &error),
        codes_handle_delete);
    if (!handle)
      break;
    size_t size = 0;
    error = codes_get_size(handle.get(), "values", &size);
    if (error || !size) {
      std::cerr << "Cannot decode values in " << path << "\n";
      return 1;
    }
    std::vector<double> values(size), latitudes(size), longitudes(size);
    error = codes_grib_get_data(handle.get(), latitudes.data(), longitudes.data(), values.data());
    if (error) {
      std::cerr << codes_get_error_message(error) << " in " << path << "\n";
      return 1;
    }
    const auto limits = std::minmax_element(values.begin(), values.end());
    std::cout << "message=" << ++count << " points=" << size
              << " min=" << *limits.first << " max=" << *limits.second << "\n";
  }
  if ((error && error != CODES_END_OF_FILE) || count == 0) {
    std::cerr << "Invalid or empty GRIB: " << path << " ("
              << codes_get_error_message(error) << ")\n";
    return 1;
  }
  std::cout << "messages=" << count << "\n";
  return 0;
}
