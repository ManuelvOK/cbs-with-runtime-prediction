BUILDDIR=build
TARGET=$(BUILDDIR)/deadline_managed

RM=rm -rf
MKDIR=mkdir -p

PID=$(shell ps | grep zsh | xargs echo | cut -d " " -f1)

.PHONY: all
all: | $(BUILDDIR)/
all: $(TARGET)

$(BUILDDIR)/%: %.c
	gcc $< -o $@ -pthread
	sudo setcap 'cap_sys_nice=eip' $@

%/:
	$(MKDIR) $@

.PHONY: clean
clean:
	$(RM) build/

.PHONY: run
run: all
	@$(TARGET)

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
