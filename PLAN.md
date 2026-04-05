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
