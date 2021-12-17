BUILDDIR=build
TARGET=$(BUILDDIR)/deadline_managed

RM=rm -rf
MKDIR=mkdir -p

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
