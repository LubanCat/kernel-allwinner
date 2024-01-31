# SPDX-License-Identifier: GPL-2.0
dtbo-$(CONFIG_ARCH_SUNXI) += \
	h618-lubancat-i2c2-overlay.dtbo \
	h618-lubancat-i2c4-overlay.dtbo \
	h618-lubancat-pwm1-overlay.dtbo \
	h618-lubancat-spi1-overlay.dtbo \
	h618-lubancat-uart2-overlay.dtbo \
	h618-lubancat-uart5-overlay.dtbo

targets += $(dtbo-y) $(scr-y) $(dtbotxt-y)

always		:= $(dtbo-y) $(scr-y) $(dtbotxt-y)
clean-files	:= *.dtbo *.scr
