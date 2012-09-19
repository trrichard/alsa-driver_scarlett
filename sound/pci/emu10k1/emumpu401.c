/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Routines for control of EMU10K1 MPU-401 in UART mode
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

#define __NO_VERSION__
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

#define EMU10K1_MIDI_MODE_INPUT		(1<<0)
#define EMU10K1_MIDI_MODE_OUTPUT	(1<<1)

/*

 */

static void do_emu10k1_midi_interrupt(emu10k1_t *emu, emu10k1_midi_t *midi, unsigned int status)
{
	unsigned char byte, bstatus;

	if (midi->rmidi == NULL) {
		snd_emu10k1_intr_disable(emu, midi->tx_enable | midi->rx_enable);
		return;
	}

	spin_lock(&midi->input_lock);
	bstatus = inb(emu->port + midi->port_stat);
	if ((status & midi->ipr_rx) && !(bstatus & 0x80)) {
		byte = inb(emu->port + midi->port_data);
		spin_unlock(&midi->input_lock);
		if (midi->substream_input)
			snd_rawmidi_receive(midi->substream_input, &byte, 1);
		spin_lock(&midi->input_lock);
	}
	spin_unlock(&midi->input_lock);

	spin_lock(&midi->output_lock);
	bstatus = inb(emu->port + midi->port_stat);
	if ((status & midi->ipr_tx) && !(bstatus & 0x40)) {
		if (midi->substream_output &&
		    snd_rawmidi_transmit(midi->substream_output, &byte, 1) == 1) {
			outb(byte, emu->port + midi->port_data);
		} else {
			snd_emu10k1_intr_disable(emu, midi->tx_enable);
		}
	}
	spin_unlock(&midi->output_lock);
}

static void snd_emu10k1_midi_interrupt(emu10k1_t *emu, unsigned int status)
{
	do_emu10k1_midi_interrupt(emu, &emu->midi, status);
}

static void snd_emu10k1_midi_interrupt2(emu10k1_t *emu, unsigned int status)
{
	do_emu10k1_midi_interrupt(emu, &emu->midi2, status);
}

/*

 */

static void snd_emu10k1_midi_cmd(emu10k1_t * emu, emu10k1_midi_t *midi, unsigned char cmd, int ack)
{
	unsigned long flags;
	int timeout, ok;

	spin_lock_irqsave(&midi->input_lock, flags);
	outb(0x00, emu->port + midi->port_data);
	for (timeout = 100000; timeout > 0 && !(inb(emu->port + midi->port_stat) & 0x80); timeout--)
		inb(emu->port + midi->port_data);
#ifdef CONFIG_SND_DEBUG
	if (timeout <= 0)
		snd_printk("midi_cmd: clear rx timeout (status = 0x%x)\n", inb(emu->port + midi->port_stat));
#endif
	outb(cmd, emu->port + midi->port_stat);
	if (ack) {
		ok = 0;
		timeout = 10000;
		while (!ok && timeout-- > 0) {
			if (!(inb(emu->port + midi->port_stat) & 0x80)) {
				if (inb(emu->port + midi->port_stat) == 0xfe)
					ok = 1;
			}
		}
	} else {
		ok = 1;
	}
	spin_unlock_irqrestore(&midi->input_lock, flags);
	if (!ok)
		snd_printk("midi_cmd: 0x%x failed at 0x%lx (status = 0x%x, data = 0x%x)!!!\n", cmd, emu->port, inb(emu->port + midi->port_stat), inb(emu->port + midi->port_data));
}

static int snd_emu10k1_midi_input_open(snd_rawmidi_substream_t * substream)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = snd_magic_cast(emu10k1_midi_t, substream->rmidi->private_data, return -ENXIO);
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	midi->midi_mode |= EMU10K1_MIDI_MODE_INPUT;
	midi->substream_input = substream;
	if (!(midi->midi_mode & EMU10K1_MIDI_MODE_OUTPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		snd_emu10k1_midi_cmd(emu, midi, 0xff, 1);	/* reset */
		snd_emu10k1_midi_cmd(emu, midi, 0x3f, 1);	/* enter UART mode */
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;
}

static int snd_emu10k1_midi_output_open(snd_rawmidi_substream_t * substream)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = snd_magic_cast(emu10k1_midi_t, substream->rmidi->private_data, return -ENXIO);
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	midi->midi_mode |= EMU10K1_MIDI_MODE_OUTPUT;
	midi->substream_output = substream;
	if (!(midi->midi_mode & EMU10K1_MIDI_MODE_INPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		snd_emu10k1_midi_cmd(emu, midi, 0xff, 1);	/* reset */
		snd_emu10k1_midi_cmd(emu, midi, 0x3f, 1);	/* enter UART mode */
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;
}

static int snd_emu10k1_midi_input_close(snd_rawmidi_substream_t * substream)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = snd_magic_cast(emu10k1_midi_t, substream->rmidi->private_data, return -ENXIO);
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	snd_emu10k1_intr_disable(emu, midi->rx_enable);
	midi->midi_mode &= ~EMU10K1_MIDI_MODE_INPUT;
	midi->substream_input = NULL;
	if (!(midi->midi_mode & EMU10K1_MIDI_MODE_OUTPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		snd_emu10k1_midi_cmd(emu, midi, 0xff, 0);	/* reset */
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;
}

static int snd_emu10k1_midi_output_close(snd_rawmidi_substream_t * substream)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = snd_magic_cast(emu10k1_midi_t, substream->rmidi->private_data, return -ENXIO);
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return -ENXIO);
	spin_lock_irqsave(&midi->open_lock, flags);
	snd_emu10k1_intr_disable(emu, midi->tx_enable);
	midi->midi_mode &= ~EMU10K1_MIDI_MODE_OUTPUT;
	midi->substream_output = NULL;
	if (!(midi->midi_mode & EMU10K1_MIDI_MODE_INPUT)) {
		spin_unlock_irqrestore(&midi->open_lock, flags);
		snd_emu10k1_midi_cmd(emu, midi, 0xff, 0);	/* reset */
	} else {
		spin_unlock_irqrestore(&midi->open_lock, flags);
	}
	return 0;
}

static void snd_emu10k1_midi_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = snd_magic_cast(emu10k1_midi_t, substream->rmidi->private_data, return -ENXIO);
	emu = midi->emu;
	snd_assert(emu, return);

	if (up)
		snd_emu10k1_intr_enable(emu, midi->rx_enable);
	else
		snd_emu10k1_intr_disable(emu, midi->rx_enable);
}

static void snd_emu10k1_midi_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	emu10k1_t *emu;
	emu10k1_midi_t *midi = snd_magic_cast(emu10k1_midi_t, substream->rmidi->private_data, return -ENXIO);
	unsigned long flags;

	emu = midi->emu;
	snd_assert(emu, return);

	if (up) {
		int max = 4;
		unsigned char byte;
	
		while (max > 0) {
			spin_lock_irqsave(&midi->output_lock, flags);
			if (!(inb(emu->port + midi->port_stat) & 0x40)) {
				if (!(midi->midi_mode & EMU10K1_MIDI_MODE_OUTPUT) ||
				    snd_rawmidi_transmit(substream, &byte, 1) != 1) {
					spin_unlock_irqrestore(&midi->output_lock, flags);
					return;
				}
				outb(byte, emu->port + midi->port_data);
				spin_unlock_irqrestore(&midi->output_lock, flags);
				// snd_printk("uart: tx = 0x%x\n", byte);
				max--;
			} else {
				spin_unlock_irqrestore(&midi->output_lock, flags);
				break;
			}
		}
		snd_emu10k1_intr_enable(emu, midi->tx_enable);
	} else {
		snd_emu10k1_intr_disable(emu, midi->tx_enable);
	}
}

/*

 */

static snd_rawmidi_ops_t snd_emu10k1_midi_output =
{
	open:		snd_emu10k1_midi_output_open,
	close:		snd_emu10k1_midi_output_close,
	trigger:	snd_emu10k1_midi_output_trigger,
};

static snd_rawmidi_ops_t snd_emu10k1_midi_input =
{
	open:		snd_emu10k1_midi_input_open,
	close:		snd_emu10k1_midi_input_close,
	trigger:	snd_emu10k1_midi_input_trigger,
};

static void snd_emu10k1_midi_free(snd_rawmidi_t *rmidi)
{
	emu10k1_midi_t *midi = snd_magic_cast(emu10k1_midi_t, rmidi->private_data, return);
	midi->interrupt = NULL;
	midi->rmidi = NULL;
}

static int __devinit emu10k1_midi_init(emu10k1_t *emu, emu10k1_midi_t *midi, int device, char *name)
{
	snd_rawmidi_t *rmidi;
	int err;

	if ((err = snd_rawmidi_new(emu->card, name, device, 1, 1, &rmidi)) < 0)
		return err;
	midi->emu = emu;
	spin_lock_init(&midi->open_lock);
	spin_lock_init(&midi->input_lock);
	spin_lock_init(&midi->output_lock);
	strcpy(rmidi->name, name);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_emu10k1_midi_output);
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_emu10k1_midi_input);
	rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT |
	                     SNDRV_RAWMIDI_INFO_INPUT |
	                     SNDRV_RAWMIDI_INFO_DUPLEX;
	rmidi->private_data = midi;
	rmidi->private_free = snd_emu10k1_midi_free;
	midi->rmidi = rmidi;
	return 0;
}

int __devinit snd_emu10k1_midi(emu10k1_t *emu)
{
	emu10k1_midi_t *midi = &emu->midi;
	int err;

	if ((err = emu10k1_midi_init(emu, midi, 0, "EMU10K1 MPU-401 (UART)")) < 0)
		return err;

	midi->tx_enable = INTE_MIDITXENABLE;
	midi->rx_enable = INTE_MIDIRXENABLE;
	midi->port_data = MUDATA;
	midi->port_stat = MUSTAT;
	midi->ipr_tx = IPR_MIDITRANSBUFEMPTY;
	midi->ipr_rx = IPR_MIDIRECVBUFEMPTY;
	midi->interrupt = snd_emu10k1_midi_interrupt;
	return 0;
}

int __devinit snd_emu10k1_audigy_midi(emu10k1_t *emu)
{
	emu10k1_midi_t *midi;
	int err;

	midi = &emu->midi;
	if ((err = emu10k1_midi_init(emu, midi, 0, "Audigy MPU-401 (UART)")) < 0)
		return err;

	midi->tx_enable = INTE_MIDITXENABLE;
	midi->rx_enable = INTE_MIDIRXENABLE;
	midi->port_data = A_MUDATA1;
	midi->port_stat = A_MUSTAT1;
	midi->ipr_tx = IPR_MIDITRANSBUFEMPTY;
	midi->ipr_rx = IPR_MIDIRECVBUFEMPTY;
	midi->interrupt = snd_emu10k1_midi_interrupt;

	midi = &emu->midi2;
	if ((err = emu10k1_midi_init(emu, midi, 1, "Audigy MPU-401 #2")) < 0)
		return err;

	midi->tx_enable = INTE_A_MIDITXENABLE2;
	midi->rx_enable = INTE_A_MIDIRXENABLE2;
	midi->port_data = A_MUDATA2;
	midi->port_stat = A_MUSTAT2;
	midi->ipr_tx = IPR_A_MIDITRANSBUFEMPTY2;
	midi->ipr_rx = IPR_A_MIDIRECVBUFEMPTY2;
	midi->interrupt = snd_emu10k1_midi_interrupt2;
	return 0;
}
