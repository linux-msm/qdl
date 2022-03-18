/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "ufs.h"
#include "qdl.h"
#include "patch.h"

struct ufs_common *ufs_common_p;
struct ufs_epilogue *ufs_epilogue_p;
struct ufs_body *ufs_body_p;
struct ufs_body *ufs_body_last;

static const char notice_bconfigdescrlock[] = "\n"
"Please pay attention that UFS provisioning is irreversible (OTP) operation unless parameter bConfigDescrLock = 0.\n"
"In order to prevent unintentional device locking the tool has the following safety:\n\n"
"	if you REALLY intend to perform OTP, please ensure that your XML includes property\n"
"	bConfigDescrLock = 1 AND provide command line parameter --finalize-provisioning.\n\n"
"	Unless you intend to lock your device, please set bConfigDescrLock = 0 in your XML\n"
"	and don't use command line parameter --finalize-provisioning.\n\n"
"In case of mismatch between CL and XML provisioning is not performed.\n\n";



bool ufs_need_provisioning(void)
{
	return !!ufs_epilogue_p;
}

struct ufs_common *ufs_parse_common_params(xmlNode *node, bool finalize_provisioning)
{
	struct ufs_common *result;
	int errors;

	result = calloc(1, sizeof(struct ufs_common));
	errors = 0;

	result->bNumberLU = attr_as_unsigned(node, "bNumberLU", &errors);
	result->bBootEnable = !!attr_as_unsigned(node, "bBootEnable", &errors);
	result->bDescrAccessEn = !!attr_as_unsigned(node, "bDescrAccessEn", &errors);
	result->bInitPowerMode = attr_as_unsigned(node, "bInitPowerMode", &errors);
	result->bHighPriorityLUN = attr_as_unsigned(node, "bHighPriorityLUN", &errors);
	result->bSecureRemovalType = attr_as_unsigned(node, "bSecureRemovalType", &errors);
	result->bInitActiveICCLevel = attr_as_unsigned(node, "bInitActiveICCLevel", &errors);
	result->wPeriodicRTCUpdate = attr_as_unsigned(node, "wPeriodicRTCUpdate", &errors);
	result->bConfigDescrLock = !!attr_as_unsigned(node, "bConfigDescrLock", &errors);

	if (errors) {
		fprintf(stderr, "[UFS] errors while parsing common\n");
		free(result);
		return NULL;
	}

	return result;
}

struct ufs_body *ufs_parse_body(xmlNode *node)
{
	struct ufs_body *result;
	int errors;

	result = calloc(1, sizeof(struct ufs_body));
	errors = 0;

	result->LUNum = attr_as_unsigned(node, "LUNum", &errors);
	result->bLUEnable = !!attr_as_unsigned(node, "bLUEnable", &errors);
	result->bBootLunID = attr_as_unsigned(node, "bBootLunID", &errors);
	result->size_in_kb = attr_as_unsigned(node, "size_in_kb", &errors);
	result->bDataReliability = attr_as_unsigned(node, "bDataReliability", &errors);
	result->bLUWriteProtect = attr_as_unsigned(node, "bLUWriteProtect", &errors);
	result->bMemoryType = attr_as_unsigned(node, "bMemoryType", &errors);
	result->bLogicalBlockSize = attr_as_unsigned(node, "bLogicalBlockSize", &errors);
	result->bProvisioningType = attr_as_unsigned(node, "bProvisioningType", &errors);
	result->wContextCapabilities = attr_as_unsigned(node, "wContextCapabilities", &errors);
	result->desc = attr_as_string(node, "desc", &errors);

	if (errors) {
		fprintf(stderr, "[UFS] errors while parsing body\n");
		free(result);
		return NULL;
	}
	return result;
}

struct ufs_epilogue *ufs_parse_epilogue(xmlNode *node)
{
	struct ufs_epilogue *result;
	int errors = 0;

	result = calloc(1, sizeof(struct ufs_epilogue));

	result->LUNtoGrow = attr_as_unsigned(node, "LUNtoGrow", &errors);

	if (errors) {
		fprintf(stderr, "[UFS] errors while parsing epilogue\n");
		free(result);
		return NULL;
	}
	return result;
}

int ufs_load(const char *ufs_file, bool finalize_provisioning)
{
	xmlNode *node;
	xmlNode *root;
	xmlDoc *doc;
	int retval = 0;
	struct ufs_body *ufs_body_tmp;

	if (ufs_common_p) {
		fprintf(stderr,
			"Only one UFS provisioning XML allowed, %s ignored\n",
			ufs_file);
		return -EEXIST;
	}

	doc = xmlReadFile(ufs_file, NULL, 0);
	if (!doc) {
		fprintf(stderr, "[UFS] failed to parse %s\n", ufs_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);

	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (xmlChar*)"ufs")) {
			fprintf(stderr, "[UFS] unrecognized tag \"%s\", ignoring\n",
				node->name);
			continue;
		}

		if (xmlGetProp(node, (xmlChar *)"bNumberLU")) {
			if (!ufs_common_p) {
				ufs_common_p = ufs_parse_common_params(node,
					finalize_provisioning);
			}
			else {
				fprintf(stderr, "[UFS] Only one common tag is allowed\n"
					"[UFS] provisioning aborted\n");
				retval = -EINVAL;
				break;
			}

			if (!ufs_common_p) {
				fprintf(stderr, "[UFS] Common tag corrupted\n"
					"[UFS] provisioning aborted\n");
				retval = -EINVAL;
				break;
			}
		} else if (xmlGetProp(node, (xmlChar *)"LUNum")) {
			ufs_body_tmp = ufs_parse_body(node);
			if(ufs_body_tmp) {
				if (ufs_body_p) {
					ufs_body_last->next = ufs_body_tmp;
					ufs_body_last = ufs_body_tmp;
				}
				else {
					ufs_body_p = ufs_body_tmp;
					ufs_body_last = ufs_body_tmp;
				}
			}
			else {
				fprintf(stderr, "[UFS] LU tag corrupted\n"
					"[UFS] provisioning aborted\n");
				retval = -EINVAL;
				break;
			}
		} else if (xmlGetProp(node, (xmlChar *)"commit")) {
			if (!ufs_epilogue_p) {
				ufs_epilogue_p = ufs_parse_epilogue(node);
				if (ufs_epilogue_p)
					continue;
			}
			else {
				fprintf(stderr, "[UFS] Only one finalizing tag is allowed\n"
					"[UFS] provisioning aborted\n");
				retval = -EINVAL;
				break;
			}

			if (!ufs_epilogue_p) {
				fprintf(stderr, "[UFS] Finalizing tag corrupted\n"
					"[UFS] provisioning aborted\n");
				retval = -EINVAL;
				break;
			}

		} else {
			fprintf(stderr, "[UFS] Unknown tag or %s corrupted\n"
				"[UFS] provisioning aborted\n", ufs_file);
			retval = -EINVAL;
			break;
		}
	}

	xmlFreeDoc(doc);

	if (!retval && (!ufs_common_p || !ufs_body_p || !ufs_epilogue_p)) {
		fprintf(stderr, "[UFS] %s seems to be incomplete\n"
			"[UFS] provisioning aborted\n", ufs_file);
		retval = -EINVAL;
	}

	if (retval){
		if (ufs_common_p) {
			free(ufs_common_p);
		}
		if (ufs_body_p) {
			free(ufs_body_p);
		}
		if (ufs_epilogue_p) {
			free(ufs_epilogue_p);
		}
		fprintf(stderr, "[UFS] %s seems to be corrupted, ignore\n", ufs_file);
		return retval;
	}
	if (!finalize_provisioning != !ufs_common_p->bConfigDescrLock) {
		fprintf(stderr,
			"[UFS] Value bConfigDescrLock %d in file %s don't match command line parameter --finalize-provisioning %d\n"
			"[UFS] provisioning aborted\n",
			ufs_common_p->bConfigDescrLock, ufs_file, finalize_provisioning);
		fprintf(stderr, notice_bconfigdescrlock);
		return -EINVAL;
	}
	return 0;
}

int ufs_provisioning_execute(struct qdl_device *qdl,
	int (*apply_ufs_common)(struct qdl_device *, struct ufs_common*),
	int (*apply_ufs_body)(struct qdl_device *, struct ufs_body*),
	int (*apply_ufs_epilogue)(struct qdl_device *, struct ufs_epilogue*, bool))
{
	int ret;
	struct ufs_body *body;

	if (ufs_common_p->bConfigDescrLock) {
		int i;
		printf("Attention!\nIrreversible provisioning will start in 5 s\n");
		for(i=5; i>0; i--) {
			printf(".\a");
			sleep(1);
		}
		printf("\n");
	}

	// Just ask a target to check the XML w/o real provisioning
	ret = apply_ufs_common(qdl, ufs_common_p);
	if (ret)
		return ret;
	for (body = ufs_body_p; body; body = body->next) {
		ret = apply_ufs_body(qdl, body);
		if (ret)
			return ret;
	}
	ret = apply_ufs_epilogue(qdl, ufs_epilogue_p, false);
	if (ret) {
		fprintf(stderr,
			"UFS provisioning impossible, provisioning XML may be corrupted\n");
		return ret;
	}

	// Real provisioning -- target didn't refuse a given XML
	ret = apply_ufs_common(qdl, ufs_common_p);
	if (ret)
		return ret;
	for (body = ufs_body_p; body; body = body->next) {
		ret = apply_ufs_body(qdl, body);
		if (ret)
			return ret;
	}
	return apply_ufs_epilogue(qdl, ufs_epilogue_p, true);
}
