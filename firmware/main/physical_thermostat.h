// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "trv_gatt_probe.h"
#include <app/server-cluster/DefaultServerCluster.h>

class PhysicalThermostat final : public chip::app::DefaultServerCluster {
    using DM = chip::app::DataModel::ActionReturnStatus;
    using Status = chip::Protocols::InteractionModel::Status;

  public:
    PhysicalThermostat(uint16_t ep, size_t slot, const bridge::EnrollmentId &id);
    CHIP_ERROR Attributes(const chip::app::ConcreteClusterPath &,
                          chip::ReadOnlyBufferBuilder<chip::app::DataModel::AttributeEntry> &) override;
    CHIP_ERROR
    AcceptedCommands(const chip::app::ConcreteClusterPath &,
                     chip::ReadOnlyBufferBuilder<chip::app::DataModel::AcceptedCommandEntry> &) override;
    DM ReadAttribute(const chip::app::DataModel::ReadAttributeRequest &,
                     chip::app::AttributeValueEncoder &) override;
    DM WriteAttribute(const chip::app::DataModel::WriteAttributeRequest &,
                      chip::app::AttributeValueDecoder &) override;
    std::optional<DM> InvokeCommand(const chip::app::DataModel::InvokeRequest &, chip::TLV::TLVReader &,
                                    chip::app::CommandHandler *) override;
    void publish(std::optional<bridge::TrvClimate> climate, std::optional<int16_t> temperature);

  private:
    Status request(int target);
    size_t slot_;
    bridge::EnrollmentId id_;
    std::optional<bridge::TrvClimate> climate_;
    std::optional<int16_t> temperature_;
};
