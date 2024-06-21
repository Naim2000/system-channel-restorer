#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "es.h"
#include "malloc.h"

int GetStoredTMD(uint64_t titleID, signed_blob** outbuf, uint32_t* outlen) {
	signed_blob* buffer = NULL;
	uint32_t size = 0;

	int ret = ES_GetStoredTMDSize(titleID, &size);
	if (ret < 0)
		return ret;

	buffer = memalign32(size);
	if (!buffer)
        return -ENOMEM;

	ret = ES_GetStoredTMD(titleID, buffer, size);
	if (ret < 0) {
		free(buffer);
		return ret;
    }

	*outbuf = buffer;
	*outlen = size;

	return 0;
}

int PickUpTaggedCerts(const signed_blob* certs, size_t len, RetailCerts* out) {
	while (certs && certs < (signed_blob*)(((void*)certs) + len)) {
		cert_header* cert = SIGNATURE_PAYLOAD(certs);
		uint16_t certid = *(uint16_t*)cert->cert_name;

		switch (certs[0]) {
			case ES_SIG_RSA4096:
				if (cert->cert_type == 0x00000001 && certid == 0x4341) {
					memcpy(&out->CA, certs, sizeof(out->CA));
				}

				break;

			case ES_SIG_RSA2048:
				if (cert->cert_type == 0x00000001) { // RSA2048
					switch (certid) {
						case 0x5853: // XS
							memcpy(&out->XS, certs, sizeof(sig_rsa2048) + sizeof(cert_rsa2048));
							break;
						case 0x4350: // CP
							memcpy(&out->CP, certs, sizeof(sig_rsa2048) + sizeof(cert_rsa2048));
							break;
					}
				}

				break;
		}


		certs = ES_NextCert(certs);
		if (!certs) return -1;
	}

	return 0;
}
