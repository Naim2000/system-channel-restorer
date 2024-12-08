#include <string.h>
#include <stdlib.h>
#include <sys/param.h>
#include <curl/curl.h>
#include <errno.h>
#include <ogc/es.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>

#include "nus.h"
#include "crypto.h"
#include "malloc.h"
#include "network.h"
#include "nand.h"

#define NUS_SERVER "nus.cdn.shop.wii.com"
typedef struct {
	uint32_t cid;
	uint32_t _padding;
	sha1 hash;
} SharedContent;

int GetInstalledTitle(int64_t titleID, struct Title* title) {
	int ret;
	char filepath[30];

	memset(title, 0, sizeof(struct Title));

	strcpy(filepath, "/sys/cert.sys");
	ret = NANDReadFileSimple(filepath, sizeof(RetailCerts), (unsigned char**)&title->certs, NULL);
	if (ret < 0)
		return ret;

	ret = GetStoredTMD(titleID, &title->s_tmd, &title->tmd_size);
	if (ret < 0)
		return ret;

	title->tmd = SIGNATURE_PAYLOAD(title->s_tmd);

	sprintf(filepath, "/ticket/%08x/%08x.tik", (uint32_t)(titleID >> 32), (uint32_t)titleID);
	ret = NANDReadFileSimple(filepath, STD_SIGNED_TIK_SIZE, (unsigned char**)&title->s_tik, &title->tik_size);
	if (ret < 0)
		return ret;

	title->ticket = SIGNATURE_PAYLOAD(title->s_tik);

	GetTitleKey(title->ticket, title->key);

	title->id = titleID;
	title->local = true;
	return 0;
}

int DownloadTitleMeta(int64_t titleID, int titleRev, struct Title* title) {
	int ret;
	char url[120];
	blob meta = {}, cetk = {};

	memset(title, 0, sizeof(struct Title));

	sprintf(url, "http://" NUS_SERVER "/ccs/download/%016llx/", titleID);

	title->certs = memalign32(sizeof(RetailCerts));
	if (!title->certs)
		return -ENOMEM;

	memset(title->certs, 0, sizeof(RetailCerts));

	if (titleRev > 0)
		sprintf(strrchr(url, '/'), "/tmd.%hu", (uint16_t)titleRev);
	else
		strcpy (strrchr(url, '/'), "/tmd");

	puts("	>> Downloading TMD...");
	ret = DownloadFile(url, DOWNLOAD_BLOB, &meta, NULL);
	if (ret < 0)
		goto fail;

	title->s_tmd = meta.ptr;
	title->tmd_size = SIGNED_TMD_SIZE(title->s_tmd);
	title->tmd = SIGNATURE_PAYLOAD(title->s_tmd);
	PickUpTaggedCerts(meta.ptr + title->tmd_size, meta.size - title->tmd_size, title->certs);

	puts("	>> Downloading ticket...");
	sprintf(strrchr(url, '/'), "/cetk");
	ret = DownloadFile(url, DOWNLOAD_BLOB, &cetk, NULL);
	if (ret < 0)
		goto fail;

	title->s_tik = cetk.ptr;
	title->tik_size = STD_SIGNED_TIK_SIZE;
	title->ticket = SIGNATURE_PAYLOAD(title->s_tik);
	PickUpTaggedCerts(cetk.ptr + title->tik_size, cetk.size - title->tik_size, title->certs);

	GetTitleKey(title->ticket, title->key);

	title->id = titleID;

	return 0;

fail:
	FreeTitle(title);
	return ret;
}

void ChangeTitleID(struct Title* title, int64_t new) {
	mbedtls_aes_context aes = {};
	aesiv iv = {};
	uint8_t cindex = title->ticket->reserved[0xb];
	if (cindex > 2)
		cindex = 0;

	iv.titleid = new;
	mbedtls_aes_setkey_enc(&aes, CommonKeys[cindex], 128);
	mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, sizeof(aeskey), iv.full, title->key, title->ticket->cipher_title_key);
	title->ticket->titleid = new;

	title->tmd->title_id = new;

}

static int PurgeTitle(int64_t titleid) {
	int ret;
	uint32_t viewcnt = 0;
	tikview* views, view ATTRIBUTE_ALIGN(0x20);

	ret = ES_DeleteTitleContent(titleid);
	if (ret && ret != -106)
		return ret;

	ret = ES_DeleteTitle(titleid);
	if (ret && ret != -106)
		return ret;

	ret = ES_GetNumTicketViews(titleid, &viewcnt);
	if (ret && ret != -106)
		return ret;

	if (!viewcnt)
		return ENOENT;

	views = memalign32(sizeof(tikview) * viewcnt);
	if (!views)
		return -ENOMEM;

	ret = ES_GetTicketViews(titleid, views, viewcnt);
	if (ret < 0)
		return ret;

	for (int i = 0; i < viewcnt; i++) {
		view = views[i];
		ret = ES_DeleteTicket(&view);
		if (ret < 0)
			break;
	}
	free(views);

	return ret;
}

int InstallTitle(struct Title* title, bool purge) {
	int ret;
	signed_blob* s_buffer = NULL;
	SharedContent* sharedContents = NULL;
	uint32_t sharedContentsCount = 0;

	if (title->ticket->reserved[0xb] != 0) {
		ChangeCommonKey(title->ticket, 0);
		Fakesign(title);
	}

	if (purge) {
		ret = PurgeTitle(title->tmd->title_id);
		if (ret < 0)
			goto finish;
	}

	ret = NANDReadFileSimple("/shared1/content.map", 0, (unsigned char**)&sharedContents, &sharedContentsCount);
	if (ret < 0)
		goto finish;

	sharedContentsCount /= sizeof(SharedContent);

	s_buffer = memalign32(MAX(title->tmd_size, title->tik_size));
	if (!s_buffer) {
		ret = -ENOMEM;
		goto finish;
	}

	puts("	>> Installing ticket...");
	memcpy(s_buffer, title->s_tik, title->tik_size);
	ret = ES_AddTicket(s_buffer, title->tik_size, (signed_blob*)title->certs, sizeof(RetailCerts), NULL, 0);
	if (ret < 0)
		goto finish;

	puts("	>> Installing TMD...");
	memcpy(s_buffer, title->s_tmd, title->tmd_size);
	ret = ES_AddTitleStart(s_buffer, title->tmd_size, (signed_blob*)title->certs, sizeof(RetailCerts), NULL, 0);
	if (ret < 0)
		goto finish;

	free(s_buffer);
	s_buffer = NULL;

	for (int i = 0; i < title->tmd->num_contents; i++) {
		tmd_content* content = title->tmd->contents + i;
		char path[120];
		void* buffer = NULL;

		if (content->type & 0x8000) {
			if (title->local) continue;

			bool found = false;
			for (SharedContent* s_content = sharedContents; s_content < sharedContents + sharedContentsCount; s_content++)
				if (memcmp(s_content->hash, content->hash, sizeof(sha1)) == 0) { found = true; break; }

			if (found) continue;
		}

		printf("	>> Installing content #%u...\n", content->index);
		int cfd = ret = ES_AddContentStart(title->tmd->title_id, content->cid);
		if (ret < 0)
			break;

		if (title->local) {
			size_t align_csize = __builtin_align_up(content->size, 0x10);

			sprintf(path, "/title/%08x/%08x/content/%08x.app", (uint32_t)(title->id >> 32), (uint32_t)title->id, content->cid);

			ret = NANDReadFileSimple(path, content->size, (unsigned char**)&buffer, NULL);
			if (ret < 0)
				break;

			mbedtls_aes_context aes = {};
			aesiv iv = { content->index };

			// Shower thought: just use ES_ExportContentData
			mbedtls_aes_setkey_enc(&aes, title->key, 128);
			mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, align_csize, iv.full, buffer, buffer);

			ret = ES_AddContentData(cfd, buffer, align_csize);
			free(buffer);
		}
		else {
			blob cdownload = { memalign32(0x20) };

			sprintf(path, "http://" NUS_SERVER "/ccs/download/%016llx/%08x", title->id, content->cid);
			ret = DownloadFile(path, DOWNLOAD_BLOB, &cdownload, NULL);
			if (ret != 0)
				break;

			if (!__builtin_is_aligned(cdownload.ptr, 0x20)) {
				buffer = memalign32(cdownload.size);
				if (!buffer) {
					ret = -ENOMEM;
					break;
				}

				memcpy(buffer, cdownload.ptr, cdownload.size);
				free(cdownload.ptr);
			}
			else {
				buffer = cdownload.ptr;
			}

			ret = ES_AddContentData(cfd, buffer, cdownload.size);
			free(buffer);
		}

		ret = ES_AddContentFinish(cfd);
		if (ret < 0)
			break;

	}

	if (!ret) {
		puts("	>> Finishing installation...");
		ret = ES_AddTitleFinish();
	}

	if (ret < 0)
		ES_AddTitleCancel();

finish:
	free(s_buffer);
	return ret;
}

static inline void zero_sig(signed_blob* blob) {
	memset(SIGNATURE_SIG(blob), 0, SIGNATURE_SIZE(blob) - 4);
}

bool Fakesign(struct Title* title) {
	sha1 hash;
	zero_sig(title->s_tik);
	zero_sig(title->s_tmd);

	for (uint16_t i = 0; i < 0xFFFFu; i++) {
		title->ticket->padding = i;
		mbedtls_sha1_ret((const unsigned char*)title->ticket, sizeof(tik), hash);
		if (!hash[0]) break;
	}
	if (hash[0]) return false;

	for (uint16_t i = 0; i < 0xFFFFu; i++) {
		title->tmd->fill3 = i;
		mbedtls_sha1_ret((const unsigned char*)title->tmd, TMD_SIZE(title->tmd), hash);
		if (!hash[0]) break;
	}
	if (hash[0]) return false;

	return true;
}

void FreeTitle(struct Title* title) {
	if (!title) return;
	free(title->certs);
	free(title->s_tmd);
	free(title->s_tik);
}
