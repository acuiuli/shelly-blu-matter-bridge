// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "bridge/enrollment.hpp"
#include <esp_err.h>
esp_err_t load_trv_bindings(bridge::Enrollment &table);
esp_err_t save_trv_bindings(const bridge::Enrollment &table);
