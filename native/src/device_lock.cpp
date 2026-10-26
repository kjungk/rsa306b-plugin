#include "rsa/device_lock.hpp"
#include "rsa/core.hpp"
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace rsa {
std::string DeviceLock::path() { return "/tmp/rsa306-native-" + std::to_string(getuid()) + ".lock"; }
DeviceLock::DeviceLock(const std::string& filename) {
    const int fd = ::open(filename.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd < 0) throw Error("device_lock_error","Cannot open the acquisition lock: " + filename,4);
    struct stat info{};
    if(fstat(fd,&info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || info.st_nlink != 1 ||
       (info.st_mode & 0077) != 0) {
        close(fd); throw Error("device_lock_error","Acquisition lock must be a private regular file owned by this user.",4);
    }
    if(flock(fd,LOCK_EX|LOCK_NB) != 0) {
        const int error = errno; close(fd);
        if(error == EWOULDBLOCK || error == EAGAIN)
            throw Error("device_busy","RSA306B is in use by another worker. Stop acquisition in RSA Monitor or SoundBase before connecting.",4);
        throw Error("device_lock_error","Cannot acquire the native device lock.",4);
    }
    fd_ = fd;
}
DeviceLock::~DeviceLock() { if(fd_ >= 0) close(fd_); }
}
