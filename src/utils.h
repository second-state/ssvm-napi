// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace SSVM {
namespace NAPI {

// Check glibcxx version
// If the version is incompatible, reporting an error message to users
// Requires: libstdc++6 >= 6.0.28 (GLIBCXX >= 3.4.28)
bool checkLibCXXVersion();


} // namespace NAPI
} // namespace SSVM
