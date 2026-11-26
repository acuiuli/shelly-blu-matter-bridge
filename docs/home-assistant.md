# Home Assistant clients and migration

The bridge exposes the installed radiator through HA's Matter integration. Which
software can drive it depends on the entity or protocol that software accepts.

| Client interface | How it works with the physical TRV |
| --- | --- |
| HA automation or thermostat with a percentage-capable `valve` output | Select the physical Matter valve. HA Core 2026.11.3 sends Open with a target for 1–100%, and Close for 0%. Read its `current_position` and availability. [HA implementation](https://github.com/home-assistant/core/blob/2026.11.3/homeassistant/components/matter/valve.py) |
| Software that selects a `climate` entity and writes a setpoint | Requires the real Thermostat endpoint, measured temperature, supported modes and confirmed setpoint writes. Only simulated climates exist today. |
| Better Thermostat 2.0.0, as installed for this pilot | The TRV selector requires `climate`. The generic adapter declares `valve_write=False`. Selecting the new physical valve as the controlled thermostat is unsupported. [Selector](https://github.com/KartoffelToby/better_thermostat/blob/2.0.0/custom_components/better_thermostat/config_flow.py) · [Generic adapter](https://github.com/KartoffelToby/better_thermostat/blob/2.0.0/custom_components/better_thermostat/adapters/generic.py) |
| Software calling Shelly gateway RPC or the old HA Shelly opening number | These clients address a vendor gateway or a specific HA entity. They need a Matter-capable output or an explicit service/entity migration before the vendor gateway can be retired. [Shelly gateway API](https://shelly-api-docs.shelly.cloud/gen2/Devices/Gen3/ShellyBluGwG3/) |

For each thermostat client, record its version, selected HA entity, outgoing action
and observed feedback for opening, closure and loss of contact. A climate client
needs independent setpoint and mode checks. Before retiring the vendor gateway,
repeat these checks after direct BLE enrollment with the original gateway absent
for the test valve. Client-specific adapters belong in their
respective projects. See the [capability matrix](matter-mapping.md).

## Better Thermostat compatibility baseline

2.0.0-Beta2 remains the original comparison baseline. Its generic adapter lacks
direct valve writes. The installation now uses official 2.0.0. Its native Shelly
adapter can address an opening number. The recorded 18% HA service action does
not identify its caller. [Beta2 generic adapter](https://github.com/KartoffelToby/better_thermostat/blob/2.0.0-Beta2/custom_components/better_thermostat/adapters/generic.py) · [Released Shelly adapter](https://github.com/KartoffelToby/better_thermostat/blob/2.0.0/custom_components/better_thermostat/adapters/shelly.py)

Prepare that contribution after HA grouping passes: discover the standard valve entity
belonging to the same bridged TRV, detect percentage support, use `valve.set_valve_position`
for 1–100 and `valve.close_valve` for zero, and read `current_position` as feedback.
Missing/unknown/unavailable feedback must never count as closed. Keep existing generic
adapter behavior for unsupported devices. Add upstream integration tests for position
writes, same-device discovery, unavailability and valve-only behavior. Review upstream
contribution requirements before posting. No upstream PR has been sent by this repository.

## Automation migration

The installation's automation files are not supplied here, so the following is a
migration template rather than a replacement for the existing boiler controller.
Inventory the existing entities, temperature-lock conditions, closure confirmation,
unavailable handling and boiler lease before changing services.

| Existing action/data | Replacement |
| --- | --- |
| `number.set_value` on valve-opening number | `valve.set_valve_position` with `position` |
| Number value used as physical feedback | `state_attr('valve.example', 'current_position')` |
| Zero-opening command | `valve.close_valve` |
| Command call completion used as closure | Wait for fresh, available physical position equal to zero |

Illustrative HA script fragment (replace entity ids and adapt timeouts to measured latency):

```yaml
sequence:
  - action: valve.close_valve
    target:
      entity_id: valve.example
  - wait_template: >-
      {{ has_value('valve.example') and
         is_state('valve.example', 'closed') and
         state_attr('valve.example', 'current_position') is number and
         state_attr('valve.example', 'current_position') == 0 }}
    timeout: "00:00:30"
    continue_on_timeout: false
```

This fragment only demonstrates service and feedback syntax. It does not establish
freshness by itself; the automation must also confirm a post-command observation or
other measured freshness evidence. An old zero must not satisfy a new closure lease.
Retain the existing boiler lease's owner, expiry, renewal and release rules unchanged.
Retain temperature locking and unavailable-state branches. Validate each automation in
isolation: delayed closure, wrong reported position, unavailable valve, HA restart,
expired lease, and return to service. Never release a boiler dependency solely because
a command was accepted. Keep a rollback copy of the original automations privately.
