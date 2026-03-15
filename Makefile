#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# ROMFS is the directory containing data to be added to RomFS, relative to the Makefile (Optional)
#---------------------------------------------------------------------------------
TARGET		:=	WookReader
BUILD		:=	build
SOURCES		:=	source source/menus/book source/menus/book-chooser source/helpers
DATA		:=	data
INCLUDES    :=  include include/menus/book include/menus/book-chooser include/helpers
ROMFS	    :=	romfs

VERSION_MAJOR := 0
VERSION_MINOR := 6
VERSION_MICRO := 7

APP_TITLE   := WookReader
APP_AUTHOR  := exorevan
APP_VERSION := ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_MICRO}-sigma
ICON := icon.jpg

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	-D__SWITCH__ $(INCLUDE) `sdl2-config --cflags`

CXXFLAGS	:= $(CFLAGS) -std=c++17 -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--allow-multiple-definition

# ── Список библиотек для линковки ────────────────────────────────────────────
#
# Порядок линковки: EXTRA_LIBS (наша libmupdf.a) идёт первой, затем LIBS_REAL.
# Это гарантирует что MuPDF найдёт свои символы FreeType/libjpeg первыми,
# а системные библиотеки заполняют только недостающее:
#
#  - freetype: НУЖЕН здесь. Системный harfbuzz (hb-ft.cc) требует функций
#    из FreeType 2.11+ (FT_Get_Transform, FT_Get_MM_Var, FT_Get_Paint и др.),
#    которых нет в урезанной FreeType внутри нашей libmupdf.a. Линкуется ПОСЛЕ
#    libmupdf.a — MuPDF берёт свои символы из неё, harfbuzz добирает остальное
#    из системной. Конфликтов нет, так как линкер уже "закрыл" символы MuPDF.
#
#  - jpeg: НУЖЕН здесь. SDL2_image (IMG_jpg.c) требует системный libjpeg
#    (jpeg_CreateCompress и др.). Наша libmupdf.a содержит внутренний libjpeg,
#    но его объектные файлы уже зафиксированы в архиве — повторной коллизии
#    не будет, так как линкер берёт из libmupdf.a первым.
#
#  - mujs/one.c исключён из Makefile.mupdf (см. там) — это устраняет
#    дублирование js* символов внутри самой libmupdf.a.
LIBS_REAL = stdc++fs SDL2_ttf SDL2_image png harfbuzz freetype jpeg turbojpeg webp archive lzma lz4 zstd bz2 config nx m

ifeq (,$(NODEBUG))
#LIBS_REAL += twili
endif

# Явная линковка нашей libmupdf.a первой, до системных библиотек.
# Флаг -Wl,--whole-archive не нужен для статической библиотеки,
# но порядок (-lmupdf перед -lfreetype и -ljpeg) критичен.
#
# ВАЖНО: используем $(TOPDIR), а не $(CURDIR).
# При рекурсивном вызове make -C build/ значение $(CURDIR) меняется
# на .../build/, и путь к libmupdf.a становится неверным.
# $(TOPDIR) всегда указывает на корень проекта (задан выше как CURDIR).
EXTRA_LIBS := $(TOPDIR)/lib/libmupdf.a

export EXTRA_LIBS
LIBS = $(EXTRA_LIBS) $(addprefix -l,$(LIBS_REAL)) $(shell sdl2-config --libs)

# ВАЖНО: если линкер выдаёт "undefined reference to _binary_NimbusRoman_*_cff"
# и подобные — директория mupdf/generated/ пуста. Сгенерируйте шрифты:
#   cd mupdf && make generate
# Это создаст .c файлы с встроенными шрифтами в mupdf/generated/

#---------------------------------------------------------------------------------
# Пути к библиотекам
#
# ВАЖНО: $(CURDIR)/mupdf убран из LIBDIRS, чтобы линкер не находил
# автоматически portlibs-версию libmupdf.a, libfreetype.a или libjpeg.a
# через стандартные пути поиска. Наша libmupdf.a подключается явно
# через EXTRA_LIBS выше.
#
# Если другим библиотекам (SDL2_image, SDL2_ttf) нужны заголовки MuPDF,
# они доступны через -I$(CURDIR)/mupdf/include в INCLUDE ниже.
#---------------------------------------------------------------------------------
LIBDIRS	:= $(PORTLIBS) $(LIBNX) $(CURDIR)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif
#---------------------------------------------------------------------------------

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES 	:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/mupdf/include \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
	ifneq (,$(findstring $(TARGET).json,$(jsons)))
		export APP_JSON := $(TOPDIR)/$(TARGET).json
	else
		ifneq (,$(findstring config.json,$(jsons)))
			export APP_JSON := $(TOPDIR)/config.json
		endif
	endif
else
	export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all mupdf mupdf-clean

#---------------------------------------------------------------------------------
# Основная сборка зависит от libmupdf.a — собираем её первой если нужно
#---------------------------------------------------------------------------------
all: $(BUILD)

# Явная зависимость: перед сборкой приложения убедимся, что libmupdf.a есть.
# Если она уже собрана, make не будет пересобирать её заново.
$(BUILD): $(CURDIR)/lib/libmupdf.a
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Сборка libmupdf.a через Makefile.switch из корня проекта
$(CURDIR)/lib/libmupdf.a:
	@echo ">>> Сборка libmupdf.a ..."
	@$(MAKE) -f $(CURDIR)/Makefile.switch

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
ifeq ($(strip $(APP_JSON)),)
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
	@rm -fr $(BUILD) $(TARGET).nsp $(TARGET).nso $(TARGET).npdm $(TARGET).elf
endif

#---------------------------------------------------------------------------------
mupdf-clean:
	@echo cleaning mupdf ...
	@$(MAKE) -f $(CURDIR)/Makefile.switch clean

#---------------------------------------------------------------------------------
# Ручная пересборка только libmupdf.a (без пересборки всего проекта)
mupdf:
	@$(MAKE) -f $(CURDIR)/Makefile.switch

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(APP_JSON)),)

all	:	$(OUTPUT).nro

ifeq ($(strip $(NO_NACP)),)
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
else
$(OUTPUT).nro	:	$(OUTPUT).elf
endif

else

all	:	$(OUTPUT).nsp

$(OUTPUT).nsp	:	$(OUTPUT).nso $(OUTPUT).npdm

$(OUTPUT).nso	:	$(OUTPUT).elf

endif

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	: $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
