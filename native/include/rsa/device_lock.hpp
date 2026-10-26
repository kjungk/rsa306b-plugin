#pragma once
#include <string>

namespace rsa {
// Cooperative exclusion across RSA Monitor and SoundBase workers for this user.
// The file is never unlinked: unlinking a locked inode would allow two owners.
class DeviceLock {
public:
    static std::string path();
    explicit DeviceLock(const std::string& filename = path());
    ~DeviceLock();
    DeviceLock(const DeviceLock&) = delete;
    DeviceLock& operator=(const DeviceLock&) = delete;
private:
    int fd_ = -1;
};
}
