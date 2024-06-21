#include <stddef.h>
#include <ogc/es.h>

typedef struct RetailCerts {
	// Certificate authority, Signature RSA4096, key type RSA2048.
	struct {
		sig_rsa4096 signature;
		cert_rsa2048 cert;
	} CA;

	// "XS". Signature RSA2048, key type RSA2048.
	struct {
		sig_rsa2048 signature;
		cert_rsa2048 cert;
	} XS;

	// "Content protection"(?). Signature RSA2048, key type RSA2048.
	struct {
		sig_rsa2048 signature;
		cert_rsa2048 cert;
	} CP;
} RetailCerts;

_Static_assert(sizeof(RetailCerts) == 0xA00, "I can't believe it's not cert.sys");

int GetStoredTMD(uint64_t titleID, signed_blob** outbuf, uint32_t* outlen);
int PickUpTaggedCerts(const signed_blob*, size_t len, RetailCerts*);

