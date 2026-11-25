// SPDX-License-Identifier: Apache-2.0
#include "trv_binding_store.h"
#include <nvs.h>

esp_err_t load_trv_bindings(bridge::Enrollment &table) {
    nvs_handle_t handle;
    auto err = nvs_open_from_partition("bridge_store", "trv_bindings", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        table = {};
        return ESP_OK;
    }
    if (err != ESP_OK)
        return err;
    std::array<uint8_t, bridge::Enrollment::encoded_size> data;
    size_t size = data.size();
    err = nvs_get_blob(handle, "v1", data.data(), &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        table = {};
        return ESP_OK;
    }
    if (err != ESP_OK)
        return err;
    auto decoded = bridge::Enrollment::decode(data.data(), size);
    if (!decoded)
        return ESP_ERR_INVALID_STATE; // never erase or silently replace a damaged binding
    table = *decoded;
    return ESP_OK;
}
esp_err_t save_trv_bindings(const bridge::Enrollment &table) {
    if (!table.valid())
        return ESP_ERR_INVALID_ARG;
    auto data = table.encode();
    nvs_handle_t handle;
    auto err = nvs_open_from_partition("bridge_store", "trv_bindings", NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = nvs_set_blob(handle, "v1", data.data(), data.size());
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK)
        return err;
    bridge::Enrollment readback;
    err = load_trv_bindings(readback);
    return err == ESP_OK && readback.encode() != data ? ESP_ERR_INVALID_STATE : err;
}
