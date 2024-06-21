#include <stdint.h>

#include "es.h"

struct Title {
	int64_t id;
	bool local;

	RetailCerts* certs;

	signed_blob* s_tmd;
	size_t tmd_size;
	struct _tmd* tmd;

	signed_blob* s_tik;
	size_t tik_size;
	struct _tik* ticket;

	aeskey key;
};

int DownloadTitleMeta(int64_t, int, struct Title*);
int GetInstalledTitle(int64_t, struct Title*);
void ChangeTitleID(struct Title*, int64_t);
bool Fakesign(struct Title*);
int InstallTitle(struct Title*, bool purge);
void FreeTitle(struct Title*);
