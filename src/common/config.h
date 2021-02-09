// SPDX-License-Identifier: Apache-2.0
//===-- ssvm/common/config.h - configure ----------------------------------===//
//
// Part of the SSVM Project.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains config that passed from configure stage.
///
//===----------------------------------------------------------------------===//
#pragma once

#include <string_view>

namespace SSVM {

using namespace std::literals::string_view_literals;

static inline std::string_view kVersionString [[maybe_unused]] =
    "0.7.3"sv;

} // namespace SSVM
