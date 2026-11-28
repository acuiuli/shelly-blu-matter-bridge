# Firmware updates: two independent investigations

## Gateway S3 OTA

The prototype enables the standard Matter OTA Requestor on endpoint 0. Two 0x3E0000
S3 application slots and OTA metadata support IDF boot rollback. H2 RCP firmware stays
pinned and USB-updatable; it is not embedded in the S3 OTA image. Never erase NVS or
`bridge_store` during an application update.

The target workflow is to import a locally built release image into Matter Server and
install it through Home Assistant. The [custom OTA import documentation](https://github.com/matter-js/matterjs-server/blob/main/README.md#importing-custom-ota-firmware-files)
is a reference for investigation; its current main branch is **not evidence that Matter
Server 9.3.0 supports the same UI/API**. Record whether the installation uses Python or
JavaScript Matter Server, exact 9.3.0 build, HA version and import mechanism. If that
baseline cannot perform the workflow, record a failed gate instead of silently upgrading.

Use the pinned SDK OTA image tool to wrap the S3 application binary with its correct
vendor/product identifiers, monotonically increasing software version and version
string. The wrapper is not a flash image or H2 image. A release process must verify
that metadata agrees with the image, publish checksums and include notices. Signing,
anti-rollback policy, trust provisioning and recovery are required before production.
Default SDK development credentials are unsuitable for a production release.

Acceptance: import the image on 9.3.0, discover it in HA, install, reboot and verify the
new S3 version, unchanged H2 checksum and retained endpoint ids/fabrics/bonds. Interrupt
download and installation separately; boot a deliberately unhealthy test image and
verify the previous application returns. In the prototype the image is marked valid
only after model and Thread initialization, a complete state publication and BLE host
synchronization succeed. That boot check alone does not prove network or valve health.
Qualify the health policy on hardware before relying on rollback.

## TRV vendor firmware

The bridge reads the installed build ID from the enrolled radiator and publishes
it as `BridgedDeviceBasicInformation.SoftwareVersionString` on the radiator's
endpoint. Home Assistant uses this attribute for the child device's firmware field.
The parent device's firmware field identifies the S3 application.
[Hardware validation](validation/2026-11-28-installed-firmware.md) · [Home Assistant Matter adapter](https://github.com/home-assistant/core/blob/dev/homeassistant/components/matter/adapter.py)

Gateway OTA success says nothing about TRV updates. Retain the TRV vendor firmware.
Investigate each item independently and record evidence in the capability matrix:

1. Obtain the correct vendor image and metadata through an authorized acquisition
   path, including model/version compatibility, integrity and authenticity checks.
2. Establish redistribution terms. Do not mirror vendor images or include them in
   releases without permission; user-supplied acquisition may be necessary.
3. Document BLE transfer, authentication, chunking, progress, battery constraints,
   version confirmation and reconnect behavior on the actual installed TRV firmware.
4. Test interrupted transfer, lost power, wrong model and recovery with a spare TRV;
   preserve a vendor-supported recovery path.
5. Find legitimate standard Matter exposure for bridged update availability, initiation
   and progress. No vendor cluster, overloaded attribute or fake root OTA target.

No image acquisition/redistribution permission, BLE transfer implementation or standard
bridged update mapping has been established. These remain full-parity release blockers.
