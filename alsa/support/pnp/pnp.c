/*
 *  Plug & Play 2.5 layer compatibility
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/config.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 2, 0)
#error "This driver is designed only for Linux 2.2.0 and higher."
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 3, 11)
#define NEW_RESOURCE
#endif

#ifdef ALSA_BUILD
#if defined(CONFIG_MODVERSIONS) && !defined(__GENKSYMS__) && !defined(__DEPEND__)
#define MODVERSIONS
#include <linux/modversions.h>
#include "sndversions.h"
#endif
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#ifndef ALSA_BUILD
#include <linux/isapnp.h>
#include <linux/pnp.h>
#else
#ifndef CONFIG_ISAPNP_KERNEL
#include <linux/isapnp.h>
#else
#include "../isapnp/isapnp.h"
#endif
#include "pnp.h"
#endif

#ifndef __init
#define __init
#endif

#ifndef __exit
#define __exit
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
#define module_init(x)      int init_module(void) { return x(); }
#define module_exit(x)      void cleanup_module(void) { x(); }
#endif

struct pnp_driver_instance {
	struct pnp_dev * dev;
	struct pnp_driver * driver;
	struct pnp_driver_instance * next;
};

struct pnp_card_driver_instance {
	struct pnp_card_link link;
	struct pnp_dev * devs[PNP_MAX_DEVICES];
	struct pnp_card_driver_instance * next;
};

static struct pnp_driver_instance * pnp_drivers = NULL;
static struct pnp_card_driver_instance * pnp_card_drivers = NULL;

static unsigned int from_hex(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return (c - 'A') + 10;
	if (c >= 'a' && c <= 'f')
		return (c = 'a') + 10;
	return 0x0f;
}

static int parse_id(const char * id, unsigned short * vendor, unsigned short * device)
{
	if (memcmp(id, "ANYDEVS", 7) == 0) {
		*vendor = ISAPNP_ANY_ID;
		*device = ISAPNP_ANY_ID;
	} else {
		*vendor = ISAPNP_VENDOR(id[0], id[1], id[2]);
		if (memcmp(id + 3, "XXXX", 4) == 0) {
			*device = ISAPNP_ANY_ID;
		} else if (strchr(id + 3, 'X') != NULL) {
			printk(KERN_ERR "cannot detect incomplete PnP ID definition '%s'\n", id);
			return -EINVAL;
		} else {
			*device = ISAPNP_DEVICE((from_hex(id[3]) << 12) |
						(from_hex(id[4]) << 8) |
						(from_hex(id[5]) << 4) |
						from_hex(id[6]));
		}
	}
	return 0;
}

struct pnp_dev * pnp_request_card_device(struct pnp_card_link *clink, const char * id, struct pnp_dev * from)
{
	unsigned short vendor, function;
	struct pnp_dev *dev;

	if (parse_id(id, &vendor, &function) < 0)
		return NULL;
	dev = (struct pnp_dev *)isapnp_find_dev((struct isapnp_card *)clink->card, vendor, function, (struct isapnp_dev *)from);
	if (dev->p.active)
		return NULL;
	return dev;
}

void pnp_release_card_device(struct pnp_dev * dev)
{
	if (!dev->p.active)
		return;
	dev->p.deactivate((struct isapnp_dev *)dev);
}

int pnp_register_card_driver(struct pnp_card_driver * drv)
{
	unsigned short vendor, device;
	unsigned int i, res = 0;
	const struct pnp_card_device_id *cid;
	struct pnp_card *card;
	struct pnp_dev *dev;
	struct pnp_card_driver_instance *ninst = NULL;
	
	for (cid = drv->id_table; cid->id; cid++) {
	      __next_card:
		card = NULL;
		do {
		      __next:
			if (parse_id(cid->id, &vendor, &device) < 0)
				break;
			card = (struct pnp_card *)isapnp_find_card(vendor, device, (struct isapnp_card *)card);
			if (card) {
				if (ninst == NULL) {
					ninst = kmalloc(sizeof(*ninst), GFP_KERNEL);
					if (ninst == NULL)
						return res > 0 ? (int)res : -ENOMEM;
				}
				for (i = 0; i < PNP_MAX_DEVICES; i++)
					ninst->devs[i] = NULL;
				for (i = 0; i < PNP_MAX_DEVICES && cid->devs[i].id[0] != '\0'; i++) {
					if (parse_id(cid->devs[i].id, &vendor, &device) < 0) {
						cid++;
						goto __next_card;
					}
					dev = ninst->devs[i] = (struct pnp_dev *)isapnp_find_dev((struct isapnp_card *)card, vendor, device, NULL);
					if (dev == NULL)
						goto __next;
				}
				ninst->link.card = card;
				ninst->link.driver = drv;
				ninst->link.driver_data = NULL;
				if (drv->probe(&ninst->link, cid) >= 0) {
					pnp_card_drivers->next = ninst;
					pnp_card_drivers = ninst;
					ninst = NULL;
					res++;
				}
			}
		} while (card != NULL);
	}
	return res;
}

void pnp_unregister_card_driver(struct pnp_card_driver * drv)
{
	struct pnp_card_driver_instance *inst = NULL, *pinst = NULL;
	unsigned int i;
	
	for (inst = pnp_card_drivers; inst; pinst = inst, inst = inst->next) {
		if (inst->link.driver == drv) {
			if (pinst)
				pinst->next = inst->next;
			else
				pnp_card_drivers = inst->next;
			drv->remove(&inst->link);
			for (i = 0; i < PNP_MAX_DEVICES && inst->devs[i]; i++)
				pnp_release_card_device(inst->devs[i]);
			kfree(inst);
		}
	}
}

int pnp_register_driver(struct pnp_driver *drv)
{
	unsigned short vendor, function;
	unsigned int res = 0;
	const struct pnp_device_id *did;
	struct pnp_dev *dev;
	struct pnp_driver_instance *ninst = NULL;
	
	for (did = drv->id_table; did->id[0] != '\0'; did++) {
		dev = NULL;
		if (ninst == NULL) {
			ninst = kmalloc(sizeof(*ninst), GFP_KERNEL);
			if (ninst == NULL)
				return res > 0 ? (int)res : -ENOMEM;
		}
		if (parse_id(did->id, &vendor, &function) < 0)
			continue;
		dev = ninst->dev = (struct pnp_dev *)isapnp_find_dev(NULL, vendor, function, (struct isapnp_dev *)dev);
		if (dev == NULL)
			continue;
		ninst->driver = drv;
		if (drv->probe(ninst->dev, did) >= 0) {
			pnp_drivers->next = ninst;
			pnp_drivers = ninst;
			ninst = NULL;
			res++;
		}
	}
	return res;
}

void pnp_unregister_driver(struct pnp_driver *drv)
{
	struct pnp_driver_instance *inst = NULL, *pinst = NULL;
	
	for (inst = pnp_drivers; inst; pinst = inst, inst = inst->next) {
		if (inst->driver == drv) {
			if (pinst)
				pinst->next = inst->next;
			else
				pnp_drivers = inst->next;
			drv->remove(inst->dev);
			if (inst->dev->p.active)
				inst->dev->p.deactivate((struct isapnp_dev *)inst->dev);
			kfree(inst);
		}
	}
}

void pnp_init_resource_table(struct pnp_resource_table *table)
{
	unsigned int idx;

	for (idx = 0; idx < PNP_MAX_IRQ; idx++) {
		table->irq_resource[idx].name = NULL;
		table->irq_resource[idx].start = 0;
		table->irq_resource[idx].end = 0;
		table->irq_resource[idx].flags = IORESOURCE_AUTO;
	}
	for (idx = 0; idx < PNP_MAX_DMA; idx++) {
		table->dma_resource[idx].name = NULL;
		table->dma_resource[idx].start = 0;
		table->dma_resource[idx].end = 0;
		table->dma_resource[idx].flags = IORESOURCE_AUTO;
	}
	for (idx = 0; idx < PNP_MAX_PORT; idx++) {
		table->port_resource[idx].name = NULL;
		table->port_resource[idx].start = 0;
		table->port_resource[idx].end = 0;
		table->port_resource[idx].flags = IORESOURCE_AUTO;
	}
	for (idx = 0; idx < PNP_MAX_MEM; idx++) {
		table->mem_resource[idx].name = NULL;
		table->mem_resource[idx].start = 0;
		table->mem_resource[idx].end = 0;
		table->mem_resource[idx].flags = IORESOURCE_AUTO;
	}
}

int pnp_manual_config_dev(struct pnp_dev *dev, struct pnp_resource_table *res, int mode)
{
	unsigned int idx;

	for (idx = 0; idx < PNP_MAX_IRQ; idx++) {
		dev->p.irq_resource[idx].name = res->irq_resource[idx].name;
		dev->p.irq_resource[idx].start = res->irq_resource[idx].start;
		dev->p.irq_resource[idx].end = res->irq_resource[idx].end;
		dev->p.irq_resource[idx].flags = res->irq_resource[idx].flags;
	}
	for (idx = 0; idx < PNP_MAX_DMA; idx++) {
		dev->p.dma_resource[idx].name = res->dma_resource[idx].name;
		dev->p.dma_resource[idx].start = res->dma_resource[idx].start;
		dev->p.dma_resource[idx].end = res->dma_resource[idx].end;
		dev->p.dma_resource[idx].flags = res->dma_resource[idx].flags;
	}
	for (idx = 0; idx < PNP_MAX_PORT; idx++) {
		dev->p.resource[idx].name = res->port_resource[idx].name;
		dev->p.resource[idx].start = res->port_resource[idx].start;
		dev->p.resource[idx].end = res->port_resource[idx].end;
		dev->p.resource[idx].flags = res->port_resource[idx].flags;
	}
	for (idx = 0; idx < PNP_MAX_MEM; idx++) {
		dev->p.resource[idx+8].name = res->mem_resource[idx].name;
		dev->p.resource[idx+8].start = res->mem_resource[idx].start;
		dev->p.resource[idx+8].end = res->mem_resource[idx].end;
		dev->p.resource[idx+8].flags = res->mem_resource[idx].flags;
	}
	return 0;
}

int pnp_activate_dev(struct pnp_dev *dev)
{
	return dev->p.activate((struct isapnp_dev *)dev);
}

static int __init pnp_init(void)
{
	return 0;
}

static void __exit pnp_exit(void)
{
}

module_init(pnp_init)
module_exit(pnp_exit)

EXPORT_SYMBOL(pnp_request_card_device);
EXPORT_SYMBOL(pnp_release_card_device);
EXPORT_SYMBOL(pnp_register_card_driver);
EXPORT_SYMBOL(pnp_unregister_card_driver);
EXPORT_SYMBOL(pnp_register_driver);
EXPORT_SYMBOL(pnp_unregister_driver);
EXPORT_SYMBOL(pnp_init_resource_table);
EXPORT_SYMBOL(pnp_manual_config_dev);
EXPORT_SYMBOL(pnp_activate_dev);