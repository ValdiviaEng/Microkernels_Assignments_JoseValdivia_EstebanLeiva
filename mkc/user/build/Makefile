SRC_DIR	= ../src
SRC	= $(notdir $(wildcard $(SRC_DIR)/*cc))
TARGET	= $(SRC:.cc=.nova)
VPATH	= $(SRC_DIR)

STRIP	= strip

CCFLAGS+= \
	-m32 \
	-g \

CXXFLAGS+= \
	-m32 \
	-MD \
	-Os \
	-ffunction-sections \
	-fstrict-aliasing \
	-fno-rtti \
	-fno-exceptions \
	-fcheck-new \
	-fshort-enums \
	--param max-inline-insns-single=100 \
	-mregparm=3 \
	-fomit-frame-pointer \
	-fno-stack-protector \
	-fno-PIC \
	-minline-all-stringops \
	-ggdb \
	-nostdinc \

CXXFLAGS+= \
	-Wconversion \
	-Wctor-dtor-privacy \
	-Wdeprecated \
	-Winvalid-offsetof \
	-Wnon-template-friend \
	-Wold-style-cast \
	-Woverloaded-virtual \
	-Wpmf-conversions \
	-Wreorder \
	-Wsign-promo \
	-Wstrict-null-sentinel \
	-Wsynth \
	-Waggregate-return \
	-freg-struct-return \
	-Wattributes \
	-Wcast-align \
	-Wdeprecated-declarations \
	-Wextra \
	-Winline \
	-Wmissing-noreturn \
	-Wpacked \
	-Wshadow \
	-Wstack-protector \
	-Wstrict-aliasing \
	-Wswitch \
	-Wswitch-default \
	-Wswitch-enum \
	-Wsystem-headers \
	-Wunsafe-loop-optimizations \
	-Wvolatile-register-var \
	-Wdisabled-optimization \
	-Wformat \
	-Wreturn-type \
	-Wno-non-virtual-dtor \
	-Wuninitialized

.PRECIOUS : %.o %.nova.debug

all : $(TARGET)

%.nova : %.nova.debug
	cp $^ $@
	$(STRIP) $@

%.nova.debug : linker.ld start.o %.o
	$(LD) -gc-sections -N -Map=$@.map -o $@ -T $^

%.o : %.S
	$(CC) $(CCFLAGS) -c -o $@ $<

%.o : %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

cl clean cla cleanall :
	rm -f *.o *.d *.nova *.nova.debug *.map
