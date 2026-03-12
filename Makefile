# we assume that the utilities from RISC-V cross-compiler (i.e., riscv64-unknown-elf-gcc and etc.)
# are in your system PATH. To check if your environment satisfies this requirement, simple use 
# `which` command as follows:
# $ which riscv64-unknown-elf-gcc
# if you have an output path, your environment satisfy our requirement.

# ---------------------	macros --------------------------
CROSS_PREFIX 	:= riscv64-unknown-elf-
CC 				:= $(CROSS_PREFIX)gcc
AR 				:= $(CROSS_PREFIX)ar
RANLIB        	:= $(CROSS_PREFIX)ranlib

SRC_DIR        	:= .
OBJ_DIR 		:= obj
SPROJS_INCLUDE 	:= -I.  

HOSTFS_ROOT := hostfs_root
ifneq (,)
  march := -march=
  is_32bit := $(findstring 32,$(march))
  mabi := -mabi=$(if $(is_32bit),ilp32,lp64)
endif

CFLAGS        := -Wall -Werror -gdwarf-3  -fno-builtin -nostdlib -D__NO_INLINE__ -mcmodel=medany -g -Og -std=gnu99 -Wno-unused -Wno-attributes -fno-delete-null-pointer-checks -fno-PIE -fno-omit-frame-pointer $(march)
COMPILE       	:= $(CC) -MMD -MP $(CFLAGS) $(SPROJS_INCLUDE)

#---------------------	utils -----------------------
UTIL_CPPS 	:= util/*.c

UTIL_CPPS  := $(wildcard $(UTIL_CPPS))
UTIL_OBJS  :=  $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(UTIL_CPPS)))


UTIL_LIB   := $(OBJ_DIR)/util.a

#---------------------	kernel -----------------------
KERNEL_LDS  	:= kernel/kernel.lds
KERNEL_CPPS 	:= \
	kernel/*.c \
	kernel/machine/*.c \
	kernel/util/*.c

KERNEL_ASMS 	:= \
	kernel/*.S \
	kernel/machine/*.S \
	kernel/util/*.S

KERNEL_CPPS  	:= $(wildcard $(KERNEL_CPPS))
KERNEL_ASMS  	:= $(wildcard $(KERNEL_ASMS))
KERNEL_OBJS  	:=  $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(KERNEL_CPPS)))
KERNEL_OBJS  	+=  $(addprefix $(OBJ_DIR)/, $(patsubst %.S,%.o,$(KERNEL_ASMS)))

KERNEL_TARGET = $(OBJ_DIR)/riscv-pke


#---------------------	spike interface library -----------------------
SPIKE_INF_CPPS 	:= spike_interface/*.c

SPIKE_INF_CPPS  := $(wildcard $(SPIKE_INF_CPPS))
SPIKE_INF_OBJS 	:=  $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(SPIKE_INF_CPPS)))


SPIKE_INF_LIB   := $(OBJ_DIR)/spike_interface.a


#---------------------	user   -----------------------
USER_CPPS 		:= user/app_shell.c user/user_lib.c

USER_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_CPPS)))

USER_TARGET 	:= $(HOSTFS_ROOT)/bin/app_shell

USER_E_CPPS 		:= user/app_ls.c user/user_lib.c

USER_E_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_E_CPPS)))

USER_E_TARGET 	:= $(HOSTFS_ROOT)/bin/app_ls

USER_M_CPPS 		:= user/app_mkdir.c user/user_lib.c

USER_M_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_M_CPPS)))

USER_M_TARGET 	:= $(HOSTFS_ROOT)/bin/app_mkdir

USER_T_CPPS 		:= user/app_touch.c user/user_lib.c

USER_T_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_T_CPPS)))

USER_T_TARGET 	:= $(HOSTFS_ROOT)/bin/app_touch

USER_C_CPPS 		:= user/app_cat.c user/user_lib.c

USER_C_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_C_CPPS)))

USER_C_TARGET 	:= $(HOSTFS_ROOT)/bin/app_cat

USER_O_CPPS 		:= user/app_echo.c user/user_lib.c

USER_O_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_O_CPPS)))

USER_O_TARGET 	:= $(HOSTFS_ROOT)/bin/app_echo

USER_O0_CPPS 		:= user/app_echo_0.c user/user_lib.c

USER_O0_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_O0_CPPS)))

USER_O0_TARGET 	:= $(HOSTFS_ROOT)/bin/app_echo_0

USER_WC_CPPS 		:= user/app_wc.c user/user_lib.c

USER_WC_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_WC_CPPS)))

USER_WC_TARGET 	:= $(HOSTFS_ROOT)/bin/app_wc

USER_R_CPPS 		:= user/app_relativepath.c user/user_lib.c

USER_R_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_R_CPPS)))

USER_R_TARGET 	:= $(HOSTFS_ROOT)/bin/app_relativepath

USER_S_CPPS 		:= user/app_semaphore.c user/user_lib.c

USER_S_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_S_CPPS)))

USER_S_TARGET 	:= $(HOSTFS_ROOT)/bin/app_semaphore

USER_W_CPPS 		:= user/app_cow.c user/user_lib.c

USER_W_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_W_CPPS)))

USER_W_TARGET 	:= $(HOSTFS_ROOT)/bin/app_cow

USER_Q_CPPS 		:= user/app_sum_sequence.c user/user_lib.c

USER_Q_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_Q_CPPS)))

USER_Q_TARGET 	:= $(HOSTFS_ROOT)/bin/app_sum_sequence

USER_H_CPPS 		:= user/app_singlepageheap.c user/user_lib.c

USER_H_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_H_CPPS)))

USER_H_TARGET 	:= $(HOSTFS_ROOT)/bin/app_singlepageheap

USER_BACKTRACE_CPPS 		:= user/app_print_backtrace.c user/user_lib.c

USER_BACKTRACE_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_BACKTRACE_CPPS)))

USER_BACKTRACE_TARGET 	:= $(HOSTFS_ROOT)/bin/app_print_backtrace

USER_ERRORLINE_CPPS 		:= user/app_errorline.c user/user_lib.c

USER_ERRORLINE_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_ERRORLINE_CPPS)))

USER_ERRORLINE_TARGET 	:= $(HOSTFS_ROOT)/bin/app_errorline

USER_ALLOC0_CPPS 		:= user/app_alloc0.c user/user_lib.c

USER_ALLOC0_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_ALLOC0_CPPS)))

USER_ALLOC0_TARGET 	:= $(HOSTFS_ROOT)/bin/app_alloc0

USER_ALLOC1_CPPS 		:= user/app_alloc1.c user/user_lib.c

USER_ALLOC1_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_ALLOC1_CPPS)))

USER_ALLOC1_TARGET 	:= $(HOSTFS_ROOT)/bin/app_alloc1

USER_LOOP_ALLOC_CPPS 		:= user/app_loop_alloc.c user/user_lib.c

USER_LOOP_ALLOC_OBJS  		:= $(addprefix $(OBJ_DIR)/, $(patsubst %.c,%.o,$(USER_LOOP_ALLOC_CPPS)))

USER_LOOP_ALLOC_TARGET 	:= $(HOSTFS_ROOT)/bin/app_loop_alloc
#------------------------targets------------------------
$(OBJ_DIR):
	@-mkdir -p $(OBJ_DIR)	
	@-mkdir -p $(dir $(UTIL_OBJS))
	@-mkdir -p $(dir $(SPIKE_INF_OBJS))
	@-mkdir -p $(dir $(KERNEL_OBJS))
	@-mkdir -p $(dir $(USER_OBJS))
	@-mkdir -p $(dir $(USER_E_OBJS))
	@-mkdir -p $(dir $(USER_M_OBJS))
	@-mkdir -p $(dir $(USER_T_OBJS))
	@-mkdir -p $(dir $(USER_C_OBJS))
	@-mkdir -p $(dir $(USER_O_OBJS))
	@-mkdir -p $(dir $(USER_O0_OBJS))
	@-mkdir -p $(dir $(USER_WC_OBJS))
	@-mkdir -p $(dir $(USER_R_OBJS))
	@-mkdir -p $(dir $(USER_S_OBJS))
	@-mkdir -p $(dir $(USER_W_OBJS))
	@-mkdir -p $(dir $(USER_Q_OBJS))
	@-mkdir -p $(dir $(USER_H_OBJS))
	@-mkdir -p $(dir $(USER_BACKTRACE_OBJS))
	@-mkdir -p $(dir $(USER_ERRORLINE_OBJS))
	@-mkdir -p $(dir $(USER_ALLOC0_OBJS))
	@-mkdir -p $(dir $(USER_ALLOC1_OBJS))
	@-mkdir -p $(dir $(USER_LOOP_ALLOC_OBJS))

$(OBJ_DIR)/%.o : %.c
	@echo "compiling" $<
	@$(COMPILE) -c $< -o $@

$(OBJ_DIR)/%.o : %.S
	@echo "compiling" $<
	@$(COMPILE) -c $< -o $@

$(UTIL_LIB): $(OBJ_DIR) $(UTIL_OBJS)
	@echo "linking " $@	...	
	@$(AR) -rcs $@ $(UTIL_OBJS) 
	@echo "Util lib has been build into" \"$@\"
	
$(SPIKE_INF_LIB): $(OBJ_DIR) $(UTIL_OBJS) $(SPIKE_INF_OBJS)
	@echo "linking " $@	...	
	@$(AR) -rcs $@ $(SPIKE_INF_OBJS) $(UTIL_OBJS)
	@echo "Spike lib has been build into" \"$@\"

$(KERNEL_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(SPIKE_INF_LIB) $(KERNEL_OBJS) $(KERNEL_LDS)
	@echo "linking" $@ ...
	@$(COMPILE) $(KERNEL_OBJS) $(UTIL_LIB) $(SPIKE_INF_LIB) -o $@ -T $(KERNEL_LDS)
	@echo "PKE core has been built into" \"$@\"

$(USER_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"
	@cp $@ $(OBJ_DIR)

$(USER_E_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_E_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_E_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_M_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_M_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_M_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_T_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_T_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_T_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_C_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_C_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_C_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_O_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_O_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_O_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_O0_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_O0_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_O0_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_WC_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_WC_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_WC_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_R_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_R_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_R_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_S_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_S_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_S_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_W_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_W_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_W_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_Q_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_Q_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_Q_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_H_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_H_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_H_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_BACKTRACE_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_BACKTRACE_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_BACKTRACE_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_ERRORLINE_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_ERRORLINE_OBJS)
	@echo "linking" $@	...	
	-@mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_ERRORLINE_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_ALLOC0_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_ALLOC0_OBJS)
	@echo "linking" $@	...	
	@-mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_ALLOC0_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_ALLOC1_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_ALLOC1_OBJS)
	@echo "linking" $@  ...
	@-mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_ALLOC1_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

$(USER_LOOP_ALLOC_TARGET): $(OBJ_DIR) $(UTIL_LIB) $(USER_LOOP_ALLOC_OBJS)
	@echo "linking" $@  ...
	@-mkdir -p $(HOSTFS_ROOT)/bin
	@$(COMPILE) --entry=main $(USER_LOOP_ALLOC_OBJS) $(UTIL_LIB) -o $@
	@echo "User app has been built into" \"$@\"

-include $(wildcard $(OBJ_DIR)/*/*.d)
-include $(wildcard $(OBJ_DIR)/*/*/*.d)

.DEFAULT_GOAL := $(all)

all: $(KERNEL_TARGET) $(USER_TARGET) $(USER_E_TARGET) $(USER_M_TARGET) $(USER_T_TARGET) $(USER_C_TARGET) $(USER_O_TARGET) $(USER_O0_TARGET) $(USER_WC_TARGET) $(USER_R_TARGET) $(USER_S_TARGET) $(USER_W_TARGET) $(USER_Q_TARGET) $(USER_H_TARGET) $(USER_BACKTRACE_TARGET) $(USER_ERRORLINE_TARGET) $(USER_ALLOC0_TARGET) $(USER_ALLOC1_TARGET) $(USER_LOOP_ALLOC_TARGET)
.PHONY:all

run: $(KERNEL_TARGET) $(USER_TARGET) $(USER_E_TARGET) $(USER_M_TARGET) $(USER_T_TARGET) $(USER_C_TARGET) $(USER_O_TARGET) $(USER_O0_TARGET) $(USER_WC_TARGET) $(USER_R_TARGET) $(USER_S_TARGET) $(USER_W_TARGET) $(USER_Q_TARGET) $(USER_H_TARGET) $(USER_BACKTRACE_TARGET) $(USER_ERRORLINE_TARGET) $(USER_LOOP_ALLOC_TARGET)
	@echo "********************HUST PKE********************"
	spike $(KERNEL_TARGET) /bin/app_shell

run2: $(KERNEL_TARGET) $(USER_ALLOC0_TARGET) $(USER_ALLOC1_TARGET)
	@echo "********************HUST PKE (dual-core)********************"
	spike -p2 $(KERNEL_TARGET) /bin/app_alloc0 /bin/app_alloc1
gdb:$(KERNEL_TARGET) $(USER_TARGET)
	spike --rbb-port=9824 -H $(KERNEL_TARGET) $(USER_TARGET) &
	@sleep 1
	openocd -f ./.spike.cfg &
	@sleep 1
	riscv64-unknown-elf-gdb -command=./.gdbinit

# clean gdb. need openocd!
gdb_clean:
	@-kill -9 $$(lsof -i:9824 -t)
	@-kill -9 $$(lsof -i:3333 -t)
	@sleep 1

objdump:
	riscv64-unknown-elf-objdump -d $(KERNEL_TARGET) > $(OBJ_DIR)/kernel_dump
	riscv64-unknown-elf-objdump -d $(USER_TARGET) > $(OBJ_DIR)/user_dump

obj2dump:
	riscv64-unknown-elf-objdump -d $(KERNEL_TARGET) > $(OBJ_DIR)/kernel_dump
	riscv64-unknown-elf-objdump -d $(USER_ALLOC0_TARGET) > $(OBJ_DIR)/app_alloc0_dump
	riscv64-unknown-elf-objdump -d $(USER_ALLOC1_TARGET) > $(OBJ_DIR)/app_alloc1_dump

cscope:
	find ./ -name "*.c" > cscope.files
	find ./ -name "*.h" >> cscope.files
	find ./ -name "*.S" >> cscope.files
	find ./ -name "*.lds" >> cscope.files
	cscope -bqk

format:
	@python ./format.py ./

clean:
	rm -fr ${OBJ_DIR} ${HOSTFS_ROOT}/bin
