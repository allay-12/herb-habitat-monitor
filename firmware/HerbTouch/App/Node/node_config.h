#ifndef HERB_NODE_CONFIG_H
#define HERB_NODE_CONFIG_H
/* Public profile. Credentials are loaded from a private generated header.
   Regeneration does not touch this file. PB0 powers sensors for five minutes
   per fifteen-minute cycle. The carrier retains its continuous supply. */
/* Logical node is selected on the screen; credentials/topics use the MCU UID.
   See node_identity.h. No per-node network identity is compiled here. */
#define NODE_AIR_ADDRESS 2
#define NODE_AIR_BAUD 4800u
#define NODE_SOIL_ADDRESS 1
#if NODE_AIR_ADDRESS==NODE_SOIL_ADDRESS || NODE_AIR_ADDRESS<1 || NODE_AIR_ADDRESS>254 || NODE_SOIL_ADDRESS<1 || NODE_SOIL_ADDRESS>254
#error "Sensor Modbus addresses must be distinct and in1..254"
#endif
#define NODE_SENSOR_WARMUP_MS 30000u
#define NODE_SENSOR_TIMEOUT_MS 1500u
#define NODE_SENSOR_GAP_MS 200u
#define NODE_SENSOR_STALE_MS 20000u
#define NODE_MODEM_BAUD 115200u
#define NODE_SAMPLE_MS 60000u
#define NODE_SENSOR_POLL_MS 5000u
#ifndef NODE_SENSOR_DUTY_CYCLE
#define NODE_SENSOR_DUTY_CYCLE 1
#endif
#define NODE_SENSOR_CYCLE_MS 900000u
#define NODE_SENSOR_ACTIVE_MS 300000u
#define NODE_PAYLOAD_MAX 1024u
#ifndef NODE_AUTO_SENSOR
#define NODE_AUTO_SENSOR 1
#endif
#ifndef NODE_AUTO_MODEM
#define NODE_AUTO_MODEM 1
#endif
#endif
