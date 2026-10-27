#include "rsa/core.hpp"
#include "rsa/device_lock.hpp"
#include <filesystem>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>

namespace {
void child(const std::string& mode,const std::string& name) {
    wchar_t path[32768]{};
    const auto size=GetModuleFileNameW(nullptr,path,32768);
    if(!size || size==32768)throw std::runtime_error("GetModuleFileNameW failed");
    std::wstring command=L"\""+std::wstring(path)+L"\" "+std::wstring(mode.begin(),mode.end())+L" "+std::wstring(name.begin(),name.end());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))
        throw std::runtime_error("CreateProcessW failed");
    CloseHandle(process.hThread);
    const auto waited=WaitForSingleObject(process.hProcess,5000);
    DWORD result=1;
    if(waited!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,1);
    GetExitCodeProcess(process.hProcess,&result);CloseHandle(process.hProcess);
    if(waited!=WAIT_OBJECT_0 || result!=0)throw std::runtime_error("Cross-process mutex assertion failed");
}
}

int main(int argc,char** argv) {
    if(argc==3) {
        const std::string mode=argv[1];
        try {
            rsa::DeviceLock lock(argv[2]);
            if(mode=="abandon")ExitProcess(0); // Deliberately skip the destructor.
            return 1;
        } catch(const rsa::Error& error) {return mode=="conflict" && error.code=="device_busy" ? 0 : 1;}
    }
    const auto name="Local\\RSA306B-lock-test-"+std::to_string(GetCurrentProcessId());
    try {
        {rsa::DeviceLock owner(name);child("conflict",name);}
        {rsa::DeviceLock reopened(name);}
        // Keep a non-owning reference alive so the abandoned object survives
        // its child's exit and the constructor must handle WAIT_ABANDONED.
        const auto keeper=CreateMutexA(nullptr,FALSE,name.c_str());
        if(!keeper)throw std::runtime_error("CreateMutexA failed");
        try {child("abandon",name);rsa::DeviceLock recovered(name);}
        catch(...) {CloseHandle(keeper);throw;}
        CloseHandle(keeper);
        std::cout<<"Exclusive cross-process mutex, release and abandonment recovery passed.\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
#else
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
#endif
