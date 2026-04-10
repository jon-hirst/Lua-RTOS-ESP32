DONE: Fix coroutine.lua assertion at line 137 (stack overflow vs not enough memory)

Two OOM sites both returned "not enough memory" instead of "stack overflow":
1. lua/src/ldo.c:luaD_growstack — changed normal-growth realloc to raiseerror=0 and
   fall through to "stack overflow" on failure; also changed ERRORSTACKSIZE realloc
   to raiseerror=0 so OOM there doesn't clobber the "stack overflow" raise.
2. lua/src/lstate.c:luaE_extendCI — replaced luaM_new (throws LUA_ERRMEM on OOM)
   with luaM_realloc_(raiseerror=0) and explicit luaG_runerror("stack overflow").
Root cause: on ESP32 with 8MB PSRAM, heap fragmentation exhausts memory long before
LUAI_MAXSTACK=1000000 stack slots are reached, so both the value-stack realloc and
CallInfo allocation triggered "not enough memory" instead of "stack overflow".

TODO: Build and flash, then run dofile('coroutine.lua') to verify the fix

DONE: Fix coroutine.lua crash (Interrupt WDT timeout on CPU0)

Root cause: The sieve test in coroutine.lua creates 22 deeply-nested coroutine.wrap
chains. Each nesting level requires ~400 bytes of C stack (lua_resume +
luaD_rawrunprotected with its 80-byte lua_longjmp/jmp_buf + resume + luaV_execute).
22 levels × ~400 bytes = ~8800 bytes, plus base frames (~1800 bytes) plus up to 7
Xtensa longjmp trampoline frames (~450 bytes) = ~11KB total, which overflowed the
10KB Lua task stack (CONFIG_LUA_RTOS_LUA_STACK_SIZE=10240).

The overflow corrupted heap memory below the stack, including the owner field of the
FreeRTOS spinlock embedded in the stdio stderr FILE lock, setting it to
SPINLOCK_OWNER_ID_1 (0xABAB = Core 1). When report() later called fprintf(stderr),
Core 0 spun indefinitely waiting for Core 1 to release a lock it didn't hold, causing
the interrupt WDT to fire after 3 seconds.

Fix: Changed CONFIG_LUA_RTOS_LUA_STACK_SIZE from 10240 to 20480 (the Kconfig default)
in sdkconfig.
