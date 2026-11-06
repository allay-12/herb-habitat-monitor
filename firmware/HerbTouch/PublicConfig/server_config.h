#ifndef HERB_SERVER_CONFIG_H
#define HERB_SERVER_CONFIG_H

/* REDACTED: MQTT broker address and deployment port. */
#define NODE_HOST "*****"
#define NODE_TLS_PORT 0 /* REDACTED: *****. Zero is a build-only placeholder. */

/* REDACTED: MQTT broker CA certificate. */
static const unsigned char node_ca[] = "*****";
#define NODE_CA_SIZE (sizeof(node_ca) - 1u)

#endif
