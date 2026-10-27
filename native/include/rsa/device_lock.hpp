#pragma once
#include <string>

namespace rsa {
// Cooperative exclusion across native and vendor workers. On Windows the named
// mutex is shared with the optional Python API backend. The POSIX file is never
// unlinked: unlinking a locked inode would allow two owners.
class DeviceLock {
public:
    static std::string path();
    explicit DeviceLock(const std::string& filename = path());
    ~DeviceLock();
    DeviceLock(const DeviceLock&) = delete;
    DeviceLock& operator=(const DeviceLock&) = delete;
private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};
}
