/*
 * Copyright (c) 2023, Linaro Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

/* local-gpio implementation for libgpiod major version 2 */

#include "local-gpio.h"

#include <gpiod.h>

int local_gpio_init(struct local_gpio *local_gpio)
{
	struct gpiod_request_config *req_cfg;
	int i;

	req_cfg = gpiod_request_config_new();
	if (!req_cfg) {
		err(1, "Unable to allocate request config");
		return -1;
	}
	gpiod_request_config_set_consumer(req_cfg, "cdba");

	for (i = 0; i < GPIO_COUNT; ++i) {
		struct gpiod_line_settings *line_settings;
		struct gpiod_line_config *line_cfg;
		char *gpiochip_path;

		if (!local_gpio->options->gpios[i].present)
			continue;

		if (asprintf(&gpiochip_path, "/dev/%s", local_gpio->options->gpios[i].chip) < 0) {
			free(local_gpio);
			return -1;
		}

		local_gpio->gpios[i].chip = gpiod_chip_open(gpiochip_path);
		if (!local_gpio->gpios[i].chip) {
			err(1, "Unable to open gpiochip '%s'", local_gpio->options->gpios[i].chip);
			return -1;
		}

		line_settings = gpiod_line_settings_new();
		if (!line_settings) {
			err(1, "Unable to allocate gpio line settings");
			return -1;
		}
		if (gpiod_line_settings_set_direction(line_settings,
						      GPIOD_LINE_DIRECTION_OUTPUT) < 0) {
			err(1, "Unable to set line direction");
			return -1;
		}
		if (local_gpio->options->gpios[i].active_low)
			gpiod_line_settings_set_active_low(line_settings, true);
		if (gpiod_line_settings_set_output_value(line_settings,
							 GPIOD_LINE_VALUE_INACTIVE) < 0) {
			err(1, "Unable to set line output value");
			return -1;
		}

		line_cfg = gpiod_line_config_new();
		if (!line_cfg) {
			err(1, "Unable to allocate gpio line settings");
			return -1;
		}
		if (gpiod_line_config_add_line_settings(line_cfg,
							&local_gpio->options->gpios[i].offset, 1,
							line_settings) < 0) {
			err(1, "Unable to set line config");
			return -1;
		}

		local_gpio->gpios[i].line = gpiod_chip_request_lines(local_gpio->gpios[i].chip,
								     req_cfg, line_cfg);

		if (!local_gpio->gpios[i].line) {
			err(1, "Unable to request gpio %d offset %u",
			    i, local_gpio->options->gpios[i].offset);
			return -1;
		}
	}

	return 0;
}

int local_gpio_set_value(struct local_gpio *local_gpio, unsigned int gpio, bool on)
{
	return gpiod_line_request_set_value(local_gpio->gpios[gpio].line,
					    local_gpio->options->gpios[gpio].offset,
					    on ? GPIOD_LINE_VALUE_ACTIVE
					       : GPIOD_LINE_VALUE_INACTIVE);
}
