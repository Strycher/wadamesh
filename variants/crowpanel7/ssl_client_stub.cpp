// Pragmatic stub for arduino-esp32 3.x's ssl_client functions on the P4 —
// verbatim port of variants/tanmatsu/ssl_client_stub.cpp (same arduino-internal
// ABI mismatch: NetworkClientSecure.cpp's call-site mangling doesn't match
// ssl_client.cpp's definitions on this target, so the symbols are undefined at
// link). wadamesh doesn't use HTTPS yet (companion link is USB/LoRa/TCP; map
// tiles are plain HTTP), so stub the referenced symbols to let the app link.
//
// STRONG (not weak) on purpose — see the tanmatsu original for the rationale.
// TODO: revisit for the HTTPS version-check / OTA-over-WiFi (Epic E, #10).
#if defined(HAS_CROWPANEL7)
#include <IPAddress.h>
struct sslclient_context;
#define WK

WK int  start_ssl_client(sslclient_context*, const IPAddress&, unsigned long, const char*, int,
                         const char*, bool, const char*, const char*, const char*, const char*,
                         bool, const char**) { return -1; }
WK void stop_ssl_socket(sslclient_context*) {}
WK int  data_to_read(sslclient_context*) { return 0; }
WK int  send_net_data(sslclient_context*, const unsigned char*, unsigned int) { return -1; }
WK int  get_net_receive(sslclient_context*, unsigned char*, int) { return -1; }
WK int  peek_net_receive(sslclient_context*, int) { return -1; }
WK int  send_ssl_data(sslclient_context*, const unsigned char*, unsigned int) { return -1; }
WK int  get_ssl_receive(sslclient_context*, unsigned char*, int) { return -1; }
WK void ssl_init(sslclient_context*) {}
WK int  ssl_starttls_handshake(sslclient_context*) { return -1; }
#endif // HAS_CROWPANEL7
