# Home Assistant migration and Better Thermostat

## Compatibility baseline

Better Thermostat **2.0.0-Beta2 is a baseline for testing, not an already compatible
target**. For an original Shelly BLU TRV the native Shelly adapter can write the
valve-opening number with `number.set_value`. The observed 18% action matches
that path. [Shelly adapter](https://github.com/KartoffelToby/better_thermostat/blob/2.0.0-Beta2/custom_components/better_thermostat/adapters/shelly.py), [valve write helper](https://github.com/KartoffelToby/better_thermostat/blob/2.0.0-Beta2/custom_components/better_thermostat/adapters/valve_entity.py)

Its [generic adapter](https://github.com/KartoffelToby/better_thermostat/blob/2.0.0-Beta2/custom_components/better_thermostat/adapters/generic.py)
does not supply direct valve writes. Its valve-only control behavior must be preserved
in a separate upstream contribution. No Better Thermostat logic belongs in gateway firmware.

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
