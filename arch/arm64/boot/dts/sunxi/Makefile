dtb-$(CONFIG_ARCH_SUNXI) += board.dtb
dtb-$(CONFIG_ARCH_SUNXI) += sun50iw9-lubancat-a1.dtb
dtb-$(CONFIG_ARCH_SUNXI) += sun50iw9-lubancat-a0.dtb

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb
DTC_FLAGS_board += -@

subdir-y       := $(dts-dirs) overlay
