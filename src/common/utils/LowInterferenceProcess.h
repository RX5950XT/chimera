#pragma once

class QProcess;

namespace chimera::utils {

void applyLowInterferencePriority(QProcess *process);

} // namespace chimera::utils
