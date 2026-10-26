#include "rsa/core.hpp"
#include "rsa/device_lock.hpp"
#include <filesystem>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

int main() {
    char directory[]="/tmp/rsa-lock-test-XXXXXX";
    if(!mkdtemp(directory))return 1;
    const auto path=std::string(directory)+"/worker.lock";
    try {
        {
            rsa::DeviceLock owner(path);
            const auto child=fork();
            if(child<0)throw std::runtime_error("fork failed");
            if(child==0) {
                try {rsa::DeviceLock conflict(path);_exit(1);}
                catch(const rsa::Error& e) {_exit(e.code=="device_busy"?0:1);}
            }
            int status=0;
            if(waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=0)
                throw std::runtime_error("A second process acquired the lock");
        }
        {rsa::DeviceLock reopened(path);} // Releasing ownership leaves no stale PID lock.
        const auto link=std::string(directory)+"/link";
        std::filesystem::create_symlink(path,link);
        try {rsa::DeviceLock bad(link);throw std::runtime_error("Symlink accepted");}
        catch(const rsa::Error& e) {if(e.code!="device_lock_error")throw;}
        std::filesystem::remove_all(directory);
        std::cout<<"Exclusive cross-process lock, release and symlink rejection passed.\n";
    } catch(const std::exception& e) {
        std::filesystem::remove_all(directory);std::cerr<<e.what()<<'\n';return 1;
    }
}
