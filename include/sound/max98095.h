/*
 * Platform data for MAX98095
 *
 * Copyright 2011 Maxim Integrated Products
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __SOUND_MAX98095_PDATA_H__
#define __SOUND_MAX98095_PDATA_H__

/* codec platform data */
struct max98095_pdata {
	/* Analog/digital microphone configuration:
	 * 0 = analog microphone input (normal setting)
	 * 1 = digital microphone input
	 */
	unsigned int digmic_left_mode:1;
	unsigned int digmic_right_mode:1;
};

#endif
