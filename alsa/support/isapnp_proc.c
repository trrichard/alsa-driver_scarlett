/*
 *  ISA Plug & Play support
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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

static void *isapnp_alloc(long size);
struct isapnp_dev *isapnp_devices;

struct isapnp_info_buffer {
	char *buffer;		/* pointer to begin of buffer */
	char *curr;		/* current position in buffer */
	unsigned long size;	/* current size */
	unsigned long len;	/* total length of buffer */
	int stop;		/* stop flag */
	int error;		/* error code */
};

typedef struct isapnp_info_buffer isapnp_info_buffer_t;

static struct proc_dir_entry *isapnp_proc_entry = NULL;

static void isapnp_info_read(isapnp_info_buffer_t *buffer);
static void isapnp_info_write(isapnp_info_buffer_t *buffer);

int isapnp_printf(isapnp_info_buffer_t * buffer, char *fmt,...)
{
	va_list args;
	int res;
	char sbuffer[512];

	if (buffer->stop || buffer->error)
		return 0;
	va_start(args, fmt);
	res = vsprintf(sbuffer, fmt, args);
	va_end(args);
	if (buffer->size + res >= buffer->len) {
		buffer->stop = 1;
		return 0;
	}
	strcpy(buffer->curr, sbuffer);
	buffer->curr += res;
	buffer->size += res;
	return res;
}

static loff_t isapnp_info_entry_lseek(struct file *file, loff_t offset, int orig)
{
	switch (orig) {
	case 0:	/* SEEK_SET */
		file->f_pos = offset;
		return file->f_pos;
	case 1:	/* SEEK_CUR */
		file->f_pos += offset;
		return file->f_pos;
	case 2:	/* SEEK_END */
	default:
		return -EINVAL;
	}
	return -ENXIO;
}

static ssize_t isapnp_info_entry_read(struct file *file, char *buffer,
				      size_t count, loff_t * offset)
{
	isapnp_info_buffer_t *buf;
	long size = 0, size1;
	int mode;

	mode = file->f_flags & O_ACCMODE;
	if (mode != O_RDONLY)
		return -EINVAL;
	buf = (isapnp_info_buffer_t *) file->private_data;
	if (!buf)
		return -EIO;
	if (file->f_pos >= buf->size)
		return 0;
	size = buf->size < count ? buf->size : count;
	size1 = buf->size - file->f_pos;
	if (size1 < size)
		size = size1;
	if (verify_area(VERIFY_WRITE, buffer, size))
		return -EFAULT;
	copy_to_user(buffer, buf->buffer + file->f_pos, size);
	file->f_pos += size;
	return size;
}

static ssize_t isapnp_info_entry_write(struct file *file, const char *buffer,
				       size_t count, loff_t * offset)
{
	isapnp_info_buffer_t *buf;
	long size = 0, size1;
	int mode;

	mode = file->f_flags & O_ACCMODE;
	if (mode != O_WRONLY)
		return -EINVAL;
	buf = (isapnp_info_buffer_t *) file->private_data;
	if (!buf)
		return -EIO;
	if (file->f_pos < 0)
		return -EINVAL;
	if (file->f_pos >= buf->len)
		return -ENOMEM;
	size = buf->len < count ? buf->len : count;
	size1 = buf->len - file->f_pos;
	if (size1 < size)
		size = size1;
	if (verify_area(VERIFY_READ, buffer, size))
		return -EFAULT;
	copy_from_user(buf->buffer + file->f_pos, buffer, size);
	if (buf->size < file->f_pos + size)
		buf->size = file->f_pos + size;
	file->f_pos += size;
	return size;
}

static int isapnp_info_entry_open(struct inode *inode, struct file *file)
{
	isapnp_info_buffer_t *buffer;
	int mode;

	mode = file->f_flags & O_ACCMODE;
	if (mode != O_RDONLY && mode != O_WRONLY)
		return -EINVAL;
	buffer = (isapnp_info_buffer_t *)
				isapnp_alloc(sizeof(isapnp_info_buffer_t));
	if (!buffer)
		return -ENOMEM;
	buffer->len = 4 * PAGE_SIZE;
	buffer->buffer = vmalloc(buffer->len);
	if (!buffer->buffer) {
		kfree(buffer);
		return -ENOMEM;
	}
	buffer->curr = buffer->buffer;
	file->private_data = buffer;
	MOD_INC_USE_COUNT;
	if (mode == O_RDONLY)
		isapnp_info_read(buffer);
	return 0;
}

static int isapnp_info_entry_release(struct inode *inode, struct file *file)
{
	isapnp_info_buffer_t *buffer;
	int mode;

	if ((buffer = (isapnp_info_buffer_t *) file->private_data) == NULL) {
		MOD_DEC_USE_COUNT;
		return -EINVAL;
	}
	mode = file->f_flags & O_ACCMODE;
	if (mode == O_WRONLY)
		isapnp_info_write(buffer);
	vfree(buffer->buffer);
	kfree(buffer);
	MOD_DEC_USE_COUNT;
	return 0;
}

static unsigned int isapnp_info_entry_poll(struct file *file, poll_table * wait)
{
	if (!file->private_data)
		return 0;
	return POLLIN | POLLRDNORM;
}

static int isapnp_info_entry_ioctl(struct inode *inode, struct file *file,
				   unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int isapnp_info_entry_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -ENXIO;
}

static struct file_operations isapnp_info_entry_operations =
{
	isapnp_info_entry_lseek,	/* lseek */
	isapnp_info_entry_read,		/* read */
	isapnp_info_entry_write,	/* write */
	NULL,				/* readdir */
	isapnp_info_entry_poll,		/* poll */
	isapnp_info_entry_ioctl,	/* ioctl - default */
	isapnp_info_entry_mmap,		/* mmap */
	isapnp_info_entry_open,		/* open */
	NULL,				/* flush */
	isapnp_info_entry_release,	/* release */
	NULL,				/* can't fsync */
	NULL,				/* fasync */
	NULL,				/* check_media_change */
	NULL,				/* revalidate */
	NULL,				/* lock */
};

static struct inode_operations isapnp_info_entry_inode_operations =
{
	&isapnp_info_entry_operations,	/* default sound info directory file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

__initfunc(static int isapnp_proc_init(void))
{
	struct proc_dir_entry *p;

	isapnp_proc_entry = NULL;
	p = create_proc_entry("isapnp", S_IFREG | S_IRUGO | S_IWUSR, &proc_root);
	if (!p)
		return -ENOMEM;
	p->ops = &isapnp_info_entry_inode_operations;
	isapnp_proc_entry = p;
	return 0;
}

#ifdef MODULE
static int isapnp_proc_done(void)
{
	if (isapnp_proc_entry)
		proc_unregister(&proc_root, isapnp_proc_entry->low_ino);
	return 0;
}
#endif /* MODULE */

/*
 *
 */

static void isapnp_print_devid(isapnp_info_buffer_t *buffer, unsigned short vendor, unsigned short device)
{
	char tmp[8];
	
	sprintf(tmp, "%c%c%c%x%x%x%x",
			'A' + ((vendor >> 2) & 0x3f) - 1,
			'A' + (((vendor & 3) << 3) | ((vendor >> 13) & 7)) - 1,
			'A' + ((vendor >> 8) & 0x1f) - 1,
			(device >> 4) & 0x0f,
			device & 0x0f,
			(device >> 12) & 0x0f,
			(device >> 8) & 0x0f);
	isapnp_printf(buffer, tmp);
}

static void isapnp_print_compatible(isapnp_info_buffer_t *buffer, struct isapnp_compatid *compat)
{
	while (compat) {
		isapnp_printf(buffer, "    Compatible device ");
		isapnp_print_devid(buffer, compat->vendor, compat->function);
		isapnp_printf(buffer, "\n");
		compat = compat->next;
	}
}

static void isapnp_print_port(isapnp_info_buffer_t *buffer, char *space, struct isapnp_port *port)
{
	isapnp_printf(buffer, "%sPort 0x%x-0x%x, align 0x%x, size 0x%x, %i-bit address decoding\n",
			space, port->min, port->max, port->align ? (port->align-1) : 0, port->size,
			port->flags & ISAPNP_PORT_FLAG_16BITADDR ? 16 : 10);
}

static void isapnp_print_irq(isapnp_info_buffer_t *buffer, char *space, struct isapnp_irq *irq)
{
	int first = 1, i;

	isapnp_printf(buffer, "%sIRQ ", space);
	for (i = 0; i < 16; i++)
		if (irq->map & (1<<i)) {
			if (!first) {
				isapnp_printf(buffer, ",");
			} else {
				first = 0;
			}
			if (i == 2 || i == 9)
				isapnp_printf(buffer, "2/9");
			else
				isapnp_printf(buffer, "%i", i);
		}
	if (!irq->map)
		isapnp_printf(buffer, "<none>");
	if (irq->flags & ISAPNP_IRQ_FLAG_HIGHEDGE)
		isapnp_printf(buffer, " High-Edge");
	if (irq->flags & ISAPNP_IRQ_FLAG_LOWEDGE)
		isapnp_printf(buffer, " Low-Edge");
	if (irq->flags & ISAPNP_IRQ_FLAG_HIGHLEVEL)
		isapnp_printf(buffer, " High-Level");
	if (irq->flags & ISAPNP_IRQ_FLAG_LOWLEVEL)
		isapnp_printf(buffer, " Low-Level");
	isapnp_printf(buffer, "\n");
}

static void isapnp_print_dma(isapnp_info_buffer_t *buffer, char *space, struct isapnp_dma *dma)
{
	int first = 1, i;
	char *s;

	isapnp_printf(buffer, "%sDMA ", space);
	for (i = 0; i < 8; i++)
		if (dma->map & (1<<i)) {
			if (!first) {
				isapnp_printf(buffer, ",");
			} else {
				first = 0;
			}
			isapnp_printf(buffer, "%i", i);
		}
	if (!dma->map)
		isapnp_printf(buffer, "<none>");
	switch (dma->type) {
	case ISAPNP_DMA_TYPE_8BIT:
		s = "8-bit";
		break;
	case ISAPNP_DMA_TYPE_8AND16BIT:
		s = "8-bit&16-bit";
		break;
	default:
		s = "16-bit";
	}
	isapnp_printf(buffer, " %s", s);
	if (dma->flags & ISAPNP_DMA_FLAG_MASTER)
		isapnp_printf(buffer, " master");
	if (dma->flags & ISAPNP_DMA_FLAG_BYTE)
		isapnp_printf(buffer, " byte-count");
	if (dma->flags & ISAPNP_DMA_FLAG_WORD)
		isapnp_printf(buffer, " word-count");
	switch (dma->speed) {
	case ISAPNP_DMA_SPEED_TYPEA:
		s = "type-A";
		break;
	case ISAPNP_DMA_SPEED_TYPEB:
		s = "type-B";
		break;
	case ISAPNP_DMA_SPEED_TYPEF:
		s = "type-F";
		break;
	default:
		s = "compatible";
		break;
	}
	isapnp_printf(buffer, " %s\n", s);
}

static void isapnp_print_mem(isapnp_info_buffer_t *buffer, char *space, struct isapnp_mem *mem)
{
	char *s;

	isapnp_printf(buffer, "%sMemory 0x%x-0x%x, align 0x%x, size 0x%x",
			space, mem->min, mem->max, mem->align, mem->size);
	if (mem->flags & ISAPNP_MEM_FLAG_WRITEABLE)
		isapnp_printf(buffer, ", writeable");
	if (mem->flags & ISAPNP_MEM_FLAG_CACHEABLE)
		isapnp_printf(buffer, ", cacheable");
	if (mem->flags & ISAPNP_MEM_FLAG_RANGELENGTH)
		isapnp_printf(buffer, ", range-length");
	if (mem->flags & ISAPNP_MEM_FLAG_SHADOWABLE)
		isapnp_printf(buffer, ", shadowable");
	if (mem->flags & ISAPNP_MEM_FLAG_EXPANSIONROM)
		isapnp_printf(buffer, ", expansion ROM");
	switch (mem->type) {
	case ISAPNP_MEM_TYPE_8BIT:
		s = "8-bit";
		break;
	case ISAPNP_MEM_TYPE_8AND16BIT:
		s = "8-bit&16-bit";
		break;
	default:
		s = "16-bit";
	}
	isapnp_printf(buffer, ", %s\n", s);
}

static void isapnp_print_mem32(isapnp_info_buffer_t *buffer, char *space, struct isapnp_mem32 *mem32)
{
	int first = 1, i;

	isapnp_printf(buffer, "%s32-bit memory ", space);
	for (i = 0; i < 17; i++) {
		if (first) {
			first = 0;
		} else {
			isapnp_printf(buffer, ":");
		}
		isapnp_printf(buffer, "%02x", mem32->data[i]);
	}
}

static void isapnp_print_resources(isapnp_info_buffer_t *buffer, char *space, struct isapnp_resources *res)
{
	char *s;
	struct isapnp_port *port;
	struct isapnp_irq *irq;
	struct isapnp_dma *dma;
	struct isapnp_mem *mem;
	struct isapnp_mem32 *mem32;

	switch (res->priority) {
	case ISAPNP_RES_PRIORITY_PREFERRED:
		s = "preferred";
		break;
	case ISAPNP_RES_PRIORITY_ACCEPTABLE:
		s = "acceptable";
		break;
	case ISAPNP_RES_PRIORITY_FUNCTIONAL:
		s = "functional";
		break;
	default:
		s = "invalid";
	}
	isapnp_printf(buffer, "%sPriority %s\n", space, s);
	for (port = res->port; port; port = port->next)
		isapnp_print_port(buffer, space, port);
	for (irq = res->irq; irq; irq = irq->next)
		isapnp_print_irq(buffer, space, irq);
	for (dma = res->dma; dma; dma = dma->next)
		isapnp_print_dma(buffer, space, dma);
	for (mem = res->mem; mem; mem = mem->next)
		isapnp_print_mem(buffer, space, mem);
	for (mem32 = res->mem32; mem32; mem32 = mem32->next)
		isapnp_print_mem32(buffer, space, mem32);
}

static void isapnp_print_configuration(isapnp_info_buffer_t *buffer, struct isapnp_logdev *logdev)
{
	int i, tmp, next;
	char *space = "    ";

	isapnp_cfg_begin(logdev->dev->csn, logdev->number);
	isapnp_printf(buffer, "%sDevice is %sactive\n",
			space, isapnp_cfg_get_byte(ISAPNP_CFG_ACTIVATE)?"":"not ");
	for (i = next = 0; i < 8; i++) {
		tmp = isapnp_cfg_get_word(ISAPNP_CFG_PORT + (i << 1));
		if (!tmp)
			continue;
		if (!next) {
			isapnp_printf(buffer, "%sActive port ", space);
			next = 1;
		}
		isapnp_printf(buffer, "%s0x%x", i > 0 ? "," : "", tmp);
	}
	if (next)
		isapnp_printf(buffer, "\n");
	for (i = next = 0; i < 2; i++) {
		tmp = isapnp_cfg_get_word(ISAPNP_CFG_IRQ + (i << 1));
		if (!(tmp >> 8))
			continue;
		if (!next) {
			isapnp_printf(buffer, "%sActive IRQ ", space);
			next = 1;
		}
		isapnp_printf(buffer, "%s%i", i > 0 ? "," : "", tmp >> 8);
		if (tmp & 0xff)
			isapnp_printf(buffer, " [0x%x]", tmp & 0xff);
	}
	if (next)
		isapnp_printf(buffer, "\n");
	for (i = next = 0; i < 2; i++) {
		tmp = isapnp_cfg_get_byte(ISAPNP_CFG_DMA + i);
		if (tmp == 4)
			continue;
		if (!next) {
			isapnp_printf(buffer, "%sActive DMA ", space);
			next = 1;
		}
		isapnp_printf(buffer, "%s%i", i > 0 ? "," : "", tmp);
	}
	if (next)
		isapnp_printf(buffer, "\n");
	for (i = next = 0; i < 4; i++) {
		tmp = isapnp_cfg_get_dword(ISAPNP_CFG_MEM + (i << 3));
		if (!tmp)
			continue;
		if (!next) {
			isapnp_printf(buffer, "%sActive memory ", space);
			next = 1;
		}
		isapnp_printf(buffer, "%s0x%x", i > 0 ? "," : "", tmp);
	}
	if (next)
		isapnp_printf(buffer, "\n");
	isapnp_cfg_end();
}

static void isapnp_print_logdev(isapnp_info_buffer_t *buffer, struct isapnp_logdev *logdev)
{
	int block, block1;
	char *space = "    ";
	struct isapnp_resources *res, *resa;

	if (!logdev)
		return;
	isapnp_printf(buffer, "  Logical device %i '", logdev->number);
	isapnp_print_devid(buffer, logdev->vendor, logdev->function);
	isapnp_printf(buffer, ":%s'", logdev->identifier?logdev->identifier:"Unknown");
	isapnp_printf(buffer, "\n");
#if 0
	isapnp_cfg_begin(logdev->dev->csn, logdev->number);
	for (block = 0; block < 128; block++)
		if ((block % 16) == 15)
			isapnp_printf(buffer, "%02x\n", isapnp_cfg_get_byte(block));
		else
			isapnp_printf(buffer, "%02x:", isapnp_cfg_get_byte(block));
	isapnp_cfg_end();
#endif
	if (logdev->regs)
		isapnp_printf(buffer, "%sSupported registers 0x%x\n", space, logdev->regs);
	isapnp_print_compatible(buffer, logdev->compat);
	isapnp_print_configuration(buffer, logdev);
	for (res = logdev->res, block = 0; res; res = res->next, block++) {
		isapnp_printf(buffer, "%sResources %i\n", space, block);
		isapnp_print_resources(buffer, "      ", res);
		for (resa = res->alt, block1 = 1; resa; resa = resa->alt, block1++) {
			isapnp_printf(buffer, "%s  Alternate resources %i:%i\n", space, block, block1);
			isapnp_print_resources(buffer, "        ", resa);
		}
	}
}

/*
 *  Main read routine
 */
 
static void isapnp_info_read(isapnp_info_buffer_t *buffer)
{
	struct isapnp_dev *dev;
	struct isapnp_logdev *logdev;
	
	for (dev = isapnp_devices; dev; dev = dev->next) {
		isapnp_printf(buffer, "Device %i '", dev->csn);
		isapnp_print_devid(buffer, dev->vendor, dev->device);
		isapnp_printf(buffer, ":%s'", dev->identifier?dev->identifier:"Unknown");
		if (dev->pnpver)
			isapnp_printf(buffer, " PnP version %x.%x", dev->pnpver >> 4, dev->pnpver & 0x0f);
		if (dev->productver)
			isapnp_printf(buffer, " Product version %x.%x", dev->productver >> 4, dev->productver & 0x0f);
		isapnp_printf(buffer,"\n");
		for (logdev = dev->logdev; logdev; logdev = logdev->next)
			isapnp_print_logdev(buffer, logdev);
	}
}

/*
 *
 */

static struct isapnp_dev *isapnp_info_dev;
static struct isapnp_logdev *isapnp_info_logdev;

static char *isapnp_get_str(char *dest, char *src, int len)
{
	int c;

	while (*src == ' ' || *src == '\t')
		src++;
	if (*src == '"' || *src == '\'') {
		c = *src++;
		while (--len > 0 && *src && *src != c) {
			*dest++ = *src++;
		}
		if (*src == c)
			src++;
	} else {
		while (--len > 0 && *src && *src != ' ' && *src != '\t') {
			*dest++ = *src++;
		}
	}
	*dest = 0;
	while (*src == ' ' || *src == '\t')
		src++;
	return src;
}

static unsigned char isapnp_get_hex(unsigned char c)
{
	if (c >= '0' || c <= '9')
		return c - '0';
	if (c >= 'a' || c <= 'f')
		return (c - 'a') + 10;
	if (c >= 'A' || c <= 'F')
		return (c - 'A') + 10;
	return 0;
}

static unsigned int isapnp_parse_id(const char *id)
{
	if (strlen(id) != 7) {
		printk("isapnp: wrong PnP ID\n");
		return 0;
	}
	return (ISAPNP_VENDOR(id[0], id[1], id[2])<<16) |
			(isapnp_get_hex(id[3])<<4) |
			(isapnp_get_hex(id[4])<<0) |
			(isapnp_get_hex(id[5])<<12) |
			(isapnp_get_hex(id[6])<<8);
}

static int isapnp_set_device(char *line)
{
	int idx;
	unsigned int id;
	char index[16], value[32];

	isapnp_info_logdev = NULL;
	line = isapnp_get_str(index, line, sizeof(index));
	isapnp_get_str(value, line, sizeof(value));
	idx = simple_strtoul(index, NULL, 0);
	id = isapnp_parse_id(value);
	isapnp_info_dev = isapnp_find_device(id >> 16, id & 0xffff, idx);
	if (!isapnp_info_dev) {
		printk("isapnp: device '%s' order %i not found\n", value, idx);
		return 1;
	}
	if (isapnp_cfg_begin(isapnp_info_dev->csn, -1)<0) {
		printk("isapnp: configuration start sequence for device '%s' failed\n", value);
		isapnp_info_dev = NULL;
		return 1;
	}
	return 0;
}

static int isapnp_select_csn(char *line)
{
	int csn;
	char index[16], value[32];

	isapnp_info_logdev = NULL;
	isapnp_get_str(index, line, sizeof(index));
	csn = simple_strtoul(index, NULL, 0);
	for (isapnp_info_dev = isapnp_devices; isapnp_info_dev; isapnp_info_dev = isapnp_info_dev->next)
		if (isapnp_info_dev->csn == csn)
			break;
	if (!isapnp_info_dev) {
		printk("isapnp: cannot find CSN %i\n", csn);
		return 1;
	}
	if (isapnp_cfg_begin(isapnp_info_dev->csn, -1)<0) {
		printk("isapnp: configuration start sequence for device '%s' failed\n", value);
		isapnp_info_dev = NULL;
		return 1;
	}
	return 0;
}

static int isapnp_set_logdev(char *line)
{
	int idx;
	unsigned int id;
	char index[16], value[32];

	line = isapnp_get_str(index, line, sizeof(index));
	isapnp_get_str(value, line, sizeof(value));
	idx = simple_strtoul(index, NULL, 0);
	id = isapnp_parse_id(value);
	isapnp_info_logdev = isapnp_find_logdev(isapnp_info_dev, id >> 16, id & 0xffff, idx);
	if (!isapnp_info_logdev) {
		printk("isapnp: logical device '%s' order %i not found\n", value, idx);
		return 1;
	}
	isapnp_logdev(isapnp_info_logdev->number);
	return 0;
}

static int isapnp_autoconfigure(void)
{
	struct isapnp_config cfg;

	if (isapnp_config_init(&cfg, isapnp_info_logdev)<0) {
		printk("isapnp: auto-configuration initialization failed, skipping\n");
		return 0;
	}
	if (isapnp_configure(&cfg)<0) {
		printk("isapnp: auto-configuration failed (out of resources?), skipping\n");
		return 0;
	}
	return 0;
}

static int isapnp_set_port(char *line)
{
	int idx, port;
	char index[16], value[32];

	line = isapnp_get_str(index, line, sizeof(index));
	isapnp_get_str(value, line, sizeof(value));
	idx = simple_strtoul(index, NULL, 0);
	port = simple_strtoul(value, NULL, 0);
	if (idx < 0 || idx > 7) {
		printk("isapnp: wrong port index %i\n", idx);
		return 1;
	}
	if (port < 0 || port > 0xffff) {
		printk("isapnp: wrong port value 0x%x\n", port);
		return 1;
	}
	isapnp_cfg_set_word(ISAPNP_CFG_PORT + (idx << 1), port);
	return 0;
}
 
static int isapnp_set_irq(char *line)
{
	int idx, irq;
	char index[16], value[32];

	line = isapnp_get_str(index, line, sizeof(index));
	isapnp_get_str(value, line, sizeof(value));
	idx = simple_strtoul(index, NULL, 0);
	irq = simple_strtoul(value, NULL, 0);
	if (idx < 0 || idx > 1) {
		printk("isapnp: wrong IRQ index %i\n", idx);
		return 1;
	}
	if (irq == 2)
		irq = 9;
	if (irq < 0 || irq > 15) {
		printk("isapnp: wrong IRQ value %i\n", irq);
		return 1;
	}
	isapnp_cfg_set_byte(ISAPNP_CFG_IRQ + (idx << 1), irq);
	return 0;
}
 
static int isapnp_set_dma(char *line)
{
	int idx, dma;
	char index[16], value[32];

	line = isapnp_get_str(index, line, sizeof(index));
	isapnp_get_str(value, line, sizeof(value));
	idx = simple_strtoul(index, NULL, 0);
	dma = simple_strtoul(value, NULL, 0);
	if (idx < 0 || idx > 1) {
		printk("isapnp: wrong DMA index %i\n", idx);
		return 1;
	}
	if (dma < 0 || dma > 7) {
		printk("isapnp: wrong DMA value %i\n", dma);
		return 1;
	}
	isapnp_cfg_set_byte(ISAPNP_CFG_DMA + idx, dma);
	return 0;
}
 
static int isapnp_set_mem(char *line)
{
	int idx;
	unsigned int mem;
	char index[16], value[32];

	line = isapnp_get_str(index, line, sizeof(index));
	isapnp_get_str(value, line, sizeof(value));
	idx = simple_strtoul(index, NULL, 0);
	mem = simple_strtoul(value, NULL, 0);
	if (idx < 0 || idx > 3) {
		printk("isapnp: wrong memory index %i\n", idx);
		return 1;
	}
	mem >>= 8;
	isapnp_cfg_set_word(ISAPNP_CFG_MEM + (idx<<2), mem & 0xffff);
	return 0;
}
 
static int isapnp_poke(char *line, int what)
{
	int reg;
	unsigned int val;
	char index[16], value[32];

	line = isapnp_get_str(index, line, sizeof(index));
	isapnp_get_str(value, line, sizeof(value));
	reg = simple_strtoul(index, NULL, 0);
	val = simple_strtoul(value, NULL, 0);
	if (reg < 0 || reg > 127) {
		printk("isapnp: wrong register %i\n", reg);
		return 1;
	}
	switch (what) {
	case 1:
		isapnp_cfg_set_word(reg, val);
		break;
	case 2:
		isapnp_cfg_set_dword(reg, val);
		break;
	default:
		isapnp_cfg_set_byte(reg, val);
		break;
	}
	return 0;
}
 
static int isapnp_decode_line(char *line)
{
	char cmd[32];

	line = isapnp_get_str(cmd, line, sizeof(cmd));
	if (!strcmp(cmd, "dev") || !strcmp(cmd, "device"))
		return isapnp_set_device(line);
	if (!strcmp(cmd, "csn"))
		return isapnp_select_csn(line);
	if (!isapnp_info_dev) {
		printk("isapnp: device isn't selected\n");
		return 1;
	}
	if (!strcmp(cmd, "logdev") || !strcmp(cmd, "logical"))
		return isapnp_set_logdev(line);
	if (!isapnp_info_logdev) {
		printk("isapnp: logical device isn't selected\n");
		return 1;
	}
	if (!strcmp(cmd, "auto") || !strcmp(cmd, "autoconfigure"))
		return isapnp_autoconfigure();
	if (!strcmp(cmd, "act") || !strcmp(cmd, "activate")) {
		isapnp_activate(isapnp_info_logdev->number);
		return 0;
	}
	if (!strcmp(cmd, "deact") || !strcmp(cmd, "deactivate")) {
		isapnp_deactivate(isapnp_info_logdev->number);
		return 0;
	}
	if (!strcmp(cmd, "port"))
		return isapnp_set_port(line);
	if (!strcmp(cmd, "irq"))
		return isapnp_set_irq(line);
	if (!strcmp(cmd, "dma"))
		return isapnp_set_dma(line);
	if (!strcmp(cmd, "mem") || !strcmp(cmd, "memory"))
		return isapnp_set_mem(line);
	if (!strcmp(cmd, "poke"))
		return isapnp_poke(line, 0);
	if (!strcmp(cmd, "pokew"))
		return isapnp_poke(line, 1);
	if (!strcmp(cmd, "poked"))
		return isapnp_poke(line, 2);
	printk("isapnp: wrong command '%s'\n", cmd);
	return 1;
}

/*
 *  Main write routine
 */

static void isapnp_info_write(isapnp_info_buffer_t *buffer)
{
	int c, idx, idx1 = 0;
	char line[128];

	if (buffer->size <= 0)
		return;
	isapnp_info_dev = NULL;
	isapnp_info_logdev = NULL;
	for (idx = 0; idx < buffer->size; idx++) {
		c = buffer->buffer[idx];
		if (c == '\n') {
			line[idx1] = '\0';
			if (line[0] != '#') {
				if (isapnp_decode_line(line))
					goto __end;
			}
			idx1 = 0;
			continue;
		}
		if (idx1 >= sizeof(line)-1) {
			printk("isapnp: line too long, aborting\n");
			return;
		}
		line[idx1++] = c;
	}
      __end:
	if (isapnp_info_dev)
		isapnp_cfg_end();
}
