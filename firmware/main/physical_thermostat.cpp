// SPDX-License-Identifier: Apache-2.0
#include "physical_thermostat.h"
#include <algorithm>
#include <app/AttributeValueDecoder.h>
#include <app/AttributeValueEncoder.h>
#include <app/server-cluster/AttributeListBuilder.h>
#include <clusters/Thermostat/Commands.h>
#include <clusters/Thermostat/Metadata.h>

using namespace chip;
using namespace chip::app;
namespace Therm = Clusters::Thermostat;
namespace Attr = Therm::Attributes;
using Status = Protocols::InteractionModel::Status;

PhysicalThermostat::PhysicalThermostat(uint16_t ep, size_t slot, const bridge::EnrollmentId &id)
    : DefaultServerCluster({ep, Therm::Id}), slot_(slot), id_(id) {}

CHIP_ERROR PhysicalThermostat::Attributes(const ConcreteClusterPath &,
                                          ReadOnlyBufferBuilder<DataModel::AttributeEntry> &builder) {
    static constexpr DataModel::AttributeEntry attributes[] = {
        Attr::LocalTemperature::kMetadataEntry,
        Attr::ControlSequenceOfOperation::kMetadataEntry,
        Attr::SystemMode::kMetadataEntry,
        Attr::OccupiedHeatingSetpoint::kMetadataEntry,
        Attr::AbsMinHeatSetpointLimit::kMetadataEntry,
        Attr::AbsMaxHeatSetpointLimit::kMetadataEntry,
        Attr::MinHeatSetpointLimit::kMetadataEntry,
        Attr::MaxHeatSetpointLimit::kMetadataEntry};
    return AttributeListBuilder(builder).Append(Span(attributes), {});
}
CHIP_ERROR
PhysicalThermostat::AcceptedCommands(const ConcreteClusterPath &,
                                     ReadOnlyBufferBuilder<DataModel::AcceptedCommandEntry> &builder) {
    static constexpr DataModel::AcceptedCommandEntry commands[] = {
        Therm::Commands::SetpointRaiseLower::kMetadataEntry};
    return builder.ReferenceExisting(commands);
}
DataModel::ActionReturnStatus PhysicalThermostat::ReadAttribute(const DataModel::ReadAttributeRequest &r,
                                                                AttributeValueEncoder &out) {
    switch (r.path.mAttributeId) {
    case Attr::ClusterRevision::Id:
        return out.Encode(Therm::kRevision);
    case Attr::FeatureMap::Id:
        return out.Encode(uint32_t(Therm::Feature::kHeating));
    case Attr::LocalTemperature::Id:
        return temperature_ ? out.Encode(*temperature_) : out.EncodeNull();
    case Attr::ControlSequenceOfOperation::Id:
        return out.Encode(Therm::ControlSequenceOfOperationEnum::kHeatingOnly);
    case Attr::AbsMinHeatSetpointLimit::Id:
        return out.Encode(int16_t(bridge::TrvClimate::absolute_min));
    case Attr::AbsMaxHeatSetpointLimit::Id:
        return out.Encode(int16_t(bridge::TrvClimate::absolute_max));
    }
    if (!climate_)
        return Status::InvalidInState;
    switch (r.path.mAttributeId) {
    case Attr::SystemMode::Id:
        return out.Encode(climate_->functions & 1 ? Therm::SystemModeEnum::kHeat
                                                  : Therm::SystemModeEnum::kOff);
    case Attr::OccupiedHeatingSetpoint::Id:
        return out.Encode(int16_t(climate_->target));
    case Attr::MinHeatSetpointLimit::Id:
        return out.Encode(int16_t(climate_->minimum));
    case Attr::MaxHeatSetpointLimit::Id:
        return out.Encode(int16_t(climate_->maximum));
    default:
        return Status::UnsupportedAttribute;
    }
}
PhysicalThermostat::Status PhysicalThermostat::request(int target) {
    if (!climate_)
        return Status::InvalidInState;
    if (!climate_->accepts(target))
        return Status::ConstraintError;
    return queue_matter_setpoint(slot_, id_, *climate_, target) ? Status::Success : Status::Busy;
}
DataModel::ActionReturnStatus PhysicalThermostat::WriteAttribute(const DataModel::WriteAttributeRequest &r,
                                                                 AttributeValueDecoder &in) {
    if (r.path.mAttributeId == Attr::OccupiedHeatingSetpoint::Id) {
        int16_t value;
        ReturnErrorOnFailure(in.Decode(value));
        return request(value);
    }
    // Setpoint control never implicitly enables the radiator's regulator. Reject mode/limit changes.
    return Status::UnsupportedWrite;
}
std::optional<DataModel::ActionReturnStatus>
PhysicalThermostat::InvokeCommand(const DataModel::InvokeRequest &r, TLV::TLVReader &input,
                                  CommandHandler *) {
    if (r.path.mCommandId != Therm::Commands::SetpointRaiseLower::Id)
        return Status::UnsupportedCommand;
    Therm::Commands::SetpointRaiseLower::DecodableType args;
    ReturnErrorOnFailure(args.Decode(input));
    if (args.mode != Therm::SetpointRaiseLowerModeEnum::kHeat &&
        args.mode != Therm::SetpointRaiseLowerModeEnum::kBoth)
        return Status::InvalidCommand;
    if (!climate_)
        return Status::InvalidInState;
    return request(std::clamp(climate_->target + args.amount * 10, climate_->minimum, climate_->maximum));
}
void PhysicalThermostat::publish(std::optional<bridge::TrvClimate> climate,
                                 std::optional<int16_t> temperature) {
    if (temperature != temperature_) {
        temperature_ = temperature;
        NotifyAttributeChanged(Attr::LocalTemperature::Id);
    }
    const bool changed = climate.has_value() != climate_.has_value();
    if (changed || (climate && climate_->target != climate->target))
        NotifyAttributeChanged(Attr::OccupiedHeatingSetpoint::Id);
    if (changed || (climate && ((climate_->functions ^ climate->functions) & 1)))
        NotifyAttributeChanged(Attr::SystemMode::Id);
    if (changed || (climate && climate_->minimum != climate->minimum))
        NotifyAttributeChanged(Attr::MinHeatSetpointLimit::Id);
    if (changed || (climate && climate_->maximum != climate->maximum))
        NotifyAttributeChanged(Attr::MaxHeatSetpointLimit::Id);
    climate_ = climate;
}
