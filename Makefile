ifeq ($(strip $(DEVKITPRO)),)
$(error "请先设置 DEVKITPRO，例如: export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET		:=	switch-ha
BUILD		:=	build
SOURCES		:=	source
DATA		:=
INCLUDES	:=	source
ROMFS		:=	romfs

APP_TITLE	:=	Switch HA
APP_AUTHOR	:=	Switch-HA
APP_VERSION	:=	1.0.0

# hbmenu 列表里显示的图标（256x256 JPEG，来自 HA 官方 App 的桌面图标）
APP_ICON	:=	icon.jpg

# 让代码里能拿到同一个版本号（update.c 的 OTA 比较）
DEFINES		:=	-DAPP_VERSION_STR=\"$(APP_VERSION)\"

ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -ftls-model=local-exec

PKG_CONFIG	:=	$(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
SDL_CFLAGS	:=	$(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf SDL2_image)
SDL_LIBS	:=	$(shell $(PKG_CONFIG) --libs sdl2 SDL2_ttf SDL2_image)

# -Os 而不是 -O2：NRO 体积直接决定 OTA 下载时间，界面代码对极致性能不敏感
CFLAGS	:=	-g -Wall -Os -ffunction-sections -fdata-sections $(ARCH) $(DEFINES)
CFLAGS	+=	$(SDL_CFLAGS) $(INCLUDE) -D__SWITCH__

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:=	$(SDL_LIBS) -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lnx
LIBDIRS	:=	$(PORTLIBS) $(LIBNX)

# 用 g++ 驱动链接，让它处理 -specs / -m 等参数
LD	:=	$(CXX)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export ROMFSDIR	:=	$(CURDIR)/$(ROMFS)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES		:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).elf

else

DEPENDS	:=	$(OFILES:.o=.d)

NROFLAGS	:=	--icon=$(TOPDIR)/$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp --romfs=$(CURDIR)/romfs.img

$(OUTPUT).nro	:	$(OUTPUT).elf $(CURDIR)/romfs.img $(CURDIR)/$(TARGET).nacp
$(OUTPUT).elf	:	$(OFILES)

# 版本号、编译参数都写在 Makefile 里，而增量构建默认只看 .c / .h 的改动：
# 只改 APP_VERSION 时，没被改过的 update.c 不会重编，APP_VERSION_STR 就留在
# 旧值——表现为「升级后设置页还显示旧版本号」「已经是最新还提示升级」。
# 把 Makefile 作为所有源文件的隐式依赖，改任何编译参数都整体重编。
# （注意要放在 nro/elf 规则之后：写在这些规则前面会让 applog.o 变成默认目标）
$(OFILES_SRC): $(TOPDIR)/Makefile

$(CURDIR)/romfs.img: FORCE
	@build_romfs $(CURDIR)/../$(ROMFS) $@

.PHONY: FORCE
FORCE:

%.o	:	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
