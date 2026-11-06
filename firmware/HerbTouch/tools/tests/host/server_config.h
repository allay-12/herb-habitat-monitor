#ifndef HERB_TEST_SERVER_CONFIG_H
#define HERB_TEST_SERVER_CONFIG_H

/* Mock modem fixture. The payload is deliberately not a valid certificate. */
#define NODE_HOST "broker.example.invalid"
#define NODE_TLS_PORT 8883
static const unsigned char node_ca[] =
    "-----BEGIN CERTIFICATE-----\n"
    "synthetic host-test payload\n"
    "-----END CERTIFICATE-----\n";
#define NODE_CA_SIZE (sizeof(node_ca) - 1u)

#endif
