#include "SharedFolder.h"
#include "ProcessLauncher.h"

namespace chimera::storage {

SharedFolder &SharedFolder::instance() {
    static SharedFolder inst;
    return inst;
}

bool SharedFolder::addMount(const Mount &mount) {
    m_mounts.push_back(mount);
    return true;
}

void SharedFolder::removeMount(const std::string &tag) {
    for (auto it = m_mounts.begin(); it != m_mounts.end(); ++it) {
        if (it->tag == tag) {
            m_mounts.erase(it);
            return;
        }
    }
}

std::vector<SharedFolder::Mount> SharedFolder::listMounts() const {
    return m_mounts;
}

std::vector<std::string> SharedFolder::toQemuArgs() const {
    std::vector<std::string> args;
    for (const auto &m : m_mounts) {
        const std::string mode = m.readOnly ? "readonly" : "readwrite";
        args.push_back("-virtfs local,path=" + m.hostPath.string()
                       + ",mount_tag=" + m.tag
                       + ",security_model=none," + mode);
    }
    return args;
}

void SharedFolder::setAdbConfig(const std::filesystem::path &adbExe,
                                const std::string &serial) {
    m_adbExe   = adbExe;
    m_adbSerial = serial;
}

bool SharedFolder::pushToGuest(const std::filesystem::path &hostFile) const {
    const std::string guestPath =
        "/sdcard/Download/" + hostFile.filename().string();
    const auto res = chimera::instance::ProcessLauncher::runSync(
        m_adbExe.string(),
        {"-s", m_adbSerial, "push", hostFile.string(), guestPath});
    return res.exitCode == 0;
}

bool SharedFolder::pullFromGuest(const std::string &guestFilename,
                                  const std::filesystem::path &hostDir) const {
    const std::string guestPath = "/sdcard/Download/" + guestFilename;
    const auto res = chimera::instance::ProcessLauncher::runSync(
        m_adbExe.string(),
        {"-s", m_adbSerial, "pull", guestPath, hostDir.string()});
    return res.exitCode == 0;
}

} // namespace chimera::storage
