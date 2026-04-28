#pragma once
/*
 * aws_certificates.h
 * Paste your AWS IoT Core certificates here.
 *
 * How to get these:
 *   1. AWS Console → IoT Core → Manage → Things → Create Thing
 *   2. Create a certificate (auto-generate recommended)
 *   3. Download all three files:
 *      - Amazon Root CA 1          → paste into AWS_ROOT_CA
 *      - Device certificate (.crt) → paste into AWS_DEVICE_CERT
 *      - Private key (.key)        → paste into AWS_PRIVATE_KEY
 *   4. Attach a Policy that allows iot:Connect, iot:Publish, iot:Subscribe,
 *      iot:Receive on resources matching your thing name.
 *
 * NEVER commit real certificates to a public repository.
 * Add aws_certificates.h to .gitignore if your repo is public.
 */

// Amazon Root CA 1 — download from:
// https://www.amazontrust.com/repository/AmazonRootCA1.pem
static const char AWS_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
REPLACE_WITH_AMAZON_ROOT_CA_1
-----END CERTIFICATE-----
)EOF";

// Device certificate (.crt) downloaded from AWS IoT Console
static const char AWS_DEVICE_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
REPLACE_WITH_DEVICE_CERTIFICATE
-----END CERTIFICATE-----
)EOF";

// Private key (.key) downloaded from AWS IoT Console
static const char AWS_PRIVATE_KEY[] PROGMEM = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
REPLACE_WITH_PRIVATE_KEY
-----END RSA PRIVATE KEY-----
)EOF";
