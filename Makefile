BUILDDIR=build
DEPDIR    := .d

TARGET=$(BUILDDIR)/main

RM=rm -rf
MKDIR=mkdir -p

PID=$(shell ps | grep zsh | xargs echo | cut -d " " -f1)

SRCSALL := $(patsubst ./%, %, $(shell find -name "*.cc" -o -name "*.h"))
SRCSCC  := $(filter %.cc, $(SRCSALL))
SRCH    := $(filter %.h, $(SRCSALL))
OBJS    := $(patsubst %.cc, $(BUILDDIR)/%.o, $(SRCSCC))
DEPS    := $(patsubst %.cc, $(DEPDIR)/%.d, $(SRCSCC))

CXXFLAGS     := -std=c++2a -Wall -Wextra -Wpedantic -ggdb -fno-inline-small-functions -O0
DEPFLAGS     += -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
CXXFLAGSTAGS := -I/home/morion/.vim/tags

DYN_LIBS    := -pthread

.PHONY: all
all: | $(BUILDDIR)/ $(DEPDIR)/
all: $(TARGET)

$(DEPS): $(DEPDIR)/

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(DYN_LIBS)
	sudo setcap 'cap_sys_nice=eip' $@

%/:
	$(MKDIR) $@

.PHONY: clean
clean:
	$(RM) $(BUILDDIR)
	$(RM) $(DEPDIR)

.PHONY: sure
sure: clean
	@$(MAKE) --no-print-directory

.PHONY: run
run: all
	@$(TARGET)

tags: $(SRCSCC)
	$(CXX) $(CXXFLAGSTAGS) $(CXXFLAGS) -M $(SRCSCC) | sed -e 's/[\\ ]/\n/g' | \
	sed -e '/^$$/d' -e '/\.o:[ \t]*$$/d' | \
	ctags -L - --c++-kinds=+p --fields=+iaS --extra=+q -o "tags" --language-force=C++

$(OBJS): $(BUILDDIR)/%.o: %.cc $(LIB_HEADERS) $(DEPDIR)/%.d | $(DEPDIR)/
	$(CXX) $(DEPFLAGS) $(CXXFLAGS) -c -o $@ $<

.PHONY: trace
trace: all
	sudo trace-cmd record -e sched_switch $(TARGET)
	kernelshark trace.dat

CPUSET_DIR=/sys/fs/cgroup/cpuset/rt_set
.PHONY: activate
activate:
	sudo sh -c "echo -1 > /proc/sys/kernel/sched_rt_runtime_us"
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
	echo $(PID)

-include $(wildcard $(DEPS))
