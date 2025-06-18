// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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
		ux_err("errors while parsing UFS common tag\n");
		free(result);
		return NULL;
	}

	/* These parameters are optional */
	errors = 0;
	result->bWriteBoosterBufferPreserveUserSpaceEn = !!attr_as_unsigned(node, "bWriteBoosterBufferPreserveUserSpaceEn", &errors);
	result->bWriteBoosterBufferType = !!attr_as_unsigned(node, "bWriteBoosterBufferType", &errors);
	result->shared_wb_buffer_size_in_kb = attr_as_unsigned(node, "shared_wb_buffer_size_in_kb", &errors);
	result->wb = !errors;

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
		ux_err("errors while parsing UFS body tag\n");
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
		ux_err("errors while parsing UFS epilogue tag\n");
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
		ux_err("Only one UFS provisioning XML allowed, \"%s\" ignored\n",
		       ufs_file);
		return -EEXIST;
	}

	doc = xmlReadFile(ufs_file, NULL, 0);
	if (!doc) {
		ux_err("failed to parse ufs-type file \"%s\"\n", ufs_file);
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);

	for (node = root->children; node ; node = node->next) {
		if (node->type != XML_ELEMENT_NODE)
			continue;

		if (xmlStrcmp(node->name, (xmlChar *)"ufs")) {
			ux_err("unrecognized tag \"%s\" in ufs-type file \"%s\", ignoring\n",
			       ufs_file, node->name);
			continue;
		}

		if (xmlGetProp(node, (xmlChar *)"bNumberLU")) {
			if (!ufs_common_p) {
				ufs_common_p = ufs_parse_common_params(node,
								       finalize_provisioning);
			} else {
				ux_err("multiple UFS common tags found in \"%s\"\n",
				       ufs_file);
				retval = -EINVAL;
				break;
			}

			if (!ufs_common_p) {
				ux_err("invalid UFS common tag found in \"%s\"\n",
				       ufs_file);
				retval = -EINVAL;
				break;
			}
		} else if (xmlGetProp(node, (xmlChar *)"LUNum")) {
			ufs_body_tmp = ufs_parse_body(node);
			if (ufs_body_tmp) {
				if (ufs_body_p) {
					ufs_body_last->next = ufs_body_tmp;
					ufs_body_last = ufs_body_tmp;
				} else {
					ufs_body_p = ufs_body_tmp;
					ufs_body_last = ufs_body_tmp;
				}
			} else {
				ux_err("invalid UFS body tag found in \"%s\"\n",
				       ufs_file);
				retval = -EINVAL;
				break;
			}
		} else if (xmlGetProp(node, (xmlChar *)"commit")) {
			if (!ufs_epilogue_p) {
				ufs_epilogue_p = ufs_parse_epilogue(node);
				if (ufs_epilogue_p)
					continue;
			} else {
				ux_err("multiple UFS finalizing tags found in \"%s\"\n",
				       ufs_file);
				retval = -EINVAL;
				break;
			}

			if (!ufs_epilogue_p) {
				ux_err("invalid UFS finalizing tag found in \"%s\"\n",
				       ufs_file);
				retval = -EINVAL;
				break;
			}

		} else {
			ux_err("unknown tag found in ufs-type file \"%s\"\n", ufs_file);
			retval = -EINVAL;
			break;
		}
	}

	xmlFreeDoc(doc);

	if (!retval && (!ufs_common_p || !ufs_body_p || !ufs_epilogue_p)) {
		ux_err("incomplete UFS provisioning information in \"%s\"\n", ufs_file);
		retval = -EINVAL;
	}

	if (retval) {
		if (ufs_common_p) {
			free(ufs_common_p);
		}
		if (ufs_body_p) {
			free(ufs_body_p);
		}
		if (ufs_epilogue_p) {
			free(ufs_epilogue_p);
		}
		return retval;
	}
	if (!finalize_provisioning != !ufs_common_p->bConfigDescrLock) {
		ux_err("UFS provisioning value bConfigDescrLock %d in file \"%s\" don't match command line parameter --finalize-provisioning %d\n",
		       ufs_common_p->bConfigDescrLock, ufs_file, finalize_provisioning);
		ux_err(notice_bconfigdescrlock);
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

		ux_info("WARNING: irreversible provisioning will start in 5s");
		for (i = 5; i > 0; i--) {
			ux_info(".\a");
			fflush(stdout);
			sleep(1);
		}
		ux_info("\n");
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
		ux_err("UFS provisioning impossible, provisioning XML may be corrupted\n");
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
