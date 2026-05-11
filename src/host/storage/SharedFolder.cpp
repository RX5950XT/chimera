#include "SharedFolder.h"

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
    for (auto &m : m_mounts) {
        std::string mode = m.readOnly ? "readonly" : "readwrite";
        std::string arg = "-virtfs local,path=" + m.hostPath.string() +
                          ",mount_tag=" + m.tag +
                          ",security_model=none," + mode;
        args.push_back(arg);
    }
    return args;
}

} // namespace chimera::storage
