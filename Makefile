BUILDDIR  := build
SRCDIR    := .
INCDIR    := .
EXTDIR    := ext
LIBDIR    := $(EXTDIR)/lib
LIBINCDIR := $(EXTDIR)/inc
DEPDIR    := .d

TARGETNAME :=sched_sim ffplay
TARGET     :=$(patsubst %,$(BUILDDIR)/%, $(TARGETNAME))

RM    :=rm -rf
MKDIR :=mkdir -p

PID :=$(shell ps | grep zsh | xargs echo | cut -d " " -f1)

SRCSALL    := $(patsubst ./%, %, $(shell find -name "*.c*" -o -name "*.h" -o -path ./$(EXTDIR) -prune))
SRCSCC     := $(filter %.cc, $(SRCSALL))
SRCSC      := $(filter %.c, $(SRCSALL))
SRCH       := $(filter %.h, $(SRCSALL))
CXXOBJS    := $(patsubst %.cc, $(BUILDDIR)/%.o, $(SRCSCC))
COBJS      := $(patsubst %.c, $(BUILDDIR)/%.o, $(SRCSC))
ALLOBJS    := $(CXXOBJS) $(COBJS)
TARGETOBJS := $(patsubst %, $(BUILDDIR)/%.o, $(TARGETNAME))
OBJS       := $(filter-out $(TARGETOBJS), $(ALLOBJS))
DEPS       := $(patsubst %.cc, $(DEPDIR)/%.d, $(SRCSCC))
DEPS       += $(patsubst %.c, $(DEPDIR)/%.d, $(SRCSC))

CXXFLAGS     := -std=c++2a -Wall -Wextra -Wpedantic -ggdb -fno-inline-small-functions -O0
CXXFLAGS     += -I$(INCDIR) -I$(LIBINCDIR)
CFLAGS       := -Wall -Wextra -Wpedantic -ggdb -fno-inline-small-functions -O0
CFLAGS       += -I$(INCDIR) -I$(LIBINCDIR)
DEPFLAGS     += -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
CXXFLAGSTAGS := -I/home/morion/.vim/tags -I$(INCDIR)

PREDICTOR_LIB     := $(LIBDIR)/libpredictor.so
PREDICTOR_INCDIR  := $(LIBINCDIR)/predictor
PREDICTOR_EXTDIR  := $(EXTDIR)/atlas-rt
PREDICTOR_HEADERS := $(PREDICTOR_EXTDIR)/predictor

LIBRARIES   := $(PREDICTOR_LIB)
LIB_HEADERS := $(PREDICTOR_INCDIR)
DYN_LIBS    := -pthread -llttng-ust -ldl -L./$(LIBDIR) -lpredictor -Wl,-rpath=$(abspath $(LIBDIR))

PREDICTION_ENABLED ?= 0


.PHONY: all
all: $(TARGET)

$(TARGET): | $(BUILDDIR)/ $(DEPDIR)/

$(DEPS): $(DEPDIR)/

$(TARGET): $(BUILDDIR)/%: $(BUILDDIR)/%.o $(OBJS) $(LIBRARIES)
	$(CXX) -o $@ $(filter-out %.so, $^) $(DYN_LIBS)
	sudo setcap 'cap_sys_nice=eip' $@

%/:
	$(MKDIR) $@

.PHONY: clean
clean:
	$(RM) $(BUILDDIR)
	$(RM) $(DEPDIR)
	$(RM) $(LIBDIR)
	$(RM) $(LIBINCDIR)

.PHONY: sure
sure: clean
	@$(MAKE) --no-print-directory

.PHONY: run
run: all
	@$(TARGET) $(INPUT_FILE) $(PREDICTION_ENABLED)

$(BUILDDIR)/sched_sim_tracepoint.o: CXXFLAGS += -I.

tags: $(SRCSCC)
	$(CXX) $(CXXFLAGSTAGS) $(CXXFLAGS) -M $(SRCSCC) | sed -e 's/[\\ ]/\n/g' | \
	sed -e '/^$$/d' -e '/\.o:[ \t]*$$/d' | \
	ctags -L - --c++-kinds=+p --fields=+iaS --extra=+q -o "tags" --language-force=C++

$(CXXOBJS): $(BUILDDIR)/%.o: %.cc $(LIB_HEADERS) $(DEPDIR)/%.d | $(DEPDIR)/
	$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(COBJS): $(BUILDDIR)/%.o: %.c $(LIB_HEADERS) $(DEPDIR)/%.d | $(DEPDIR)/
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<

$(PREDICTOR_LIB): | $(PREDICTOR_EXTDIR)/ $(LIBDIR)/
	cd $(PREDICTOR_EXTDIR) && \
	cmake . && \
	$(MAKE) -C predictor
	ln -fs "$(CURDIR)/$(PREDICTOR_EXTDIR)/predictor/libpredictor.so" $@

$(PREDICTOR_INCDIR): $(PREDICTOR_EXTDIR) | $(LIBINCDIR)/
	ln -fs "$(CURDIR)/$</predictor" $@

CPUSET_DIR=/sys/fs/cgroup/cpuset/rt_set
.PHONY: activate
activate:
	sudo sh -c "echo -1 > /proc/sys/kernel/sched_rt_runtime_us"
	sudo $(MKDIR) /sys/fs/cgroup/cpuset
	mountpoint -q /sys/fs/cgroup/cpuset; [[ 0 -ne $$? ]] && sudo mount -t cgroup -o cpuset cpuset /sys/fs/cgroup/cpuset || true
	sudo $(MKDIR) $(CPUSET_DIR)
	sudo sh -c "echo 1 > $(CPUSET_DIR)/cpuset.cpu_exclusive"
	sudo sh -c "echo 0-1 > $(CPUSET_DIR)/cpuset.cpus"
	sudo sh -c "echo 0 > $(CPUSET_DIR)/cpuset.mems"
	sudo sh -c "echo $(PID) > $(CPUSET_DIR)/tasks"

.PHONY: deactivate
deactivate:
	sudo sh -c "echo 0 > $(CPUSET_DIR)/cpuset.cpu_exclusive"

.PHONY: debug
debug:
	echo $(SRCSALL)

-include $(wildcard $(DEPS))
