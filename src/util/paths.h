#pragma once

// Absolute paths to the plugin's ini and log.
//
// Both used to be relative to the process working directory, which is not
// reliably the game folder -- under Wine, a launcher or a mod manager the log
// would silently land somewhere else. NVSE hands us the real runtime directory,
// so use that.

namespace paths {

// `runtimeDirectory` comes from NVSEInterface::GetRuntimeDirectory and may be
// null, in which case relative paths are used as before.
void Init(const char *runtimeDirectory);

// Data\Config\PlayerMovement\, created if missing.
const char *Directory();

const char *Ini();
const char *Log();

} // namespace paths
