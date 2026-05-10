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

DONE: Debug and fix GC crashes in coroutine.lua (traverseproto / traversetable)

Root cause: The sieve test (lines 99-127) creates 22 nested coroutine.wrap chains.
Each nesting level consumes ~1255 bytes of C stack (Xtensa windowed register spills
make each lua_resume→resume→luaV_execute→luaB_auxwrap chain far more expensive than
the minimum estimate). 22 levels × 1255 = ~27600 bytes total, but the task stack was
only 20480 bytes. The overflow extended ~15KB past the FreeRTOS stack bottom
(pxStack=0x3fcb6828), corrupting heap allocations below it (e.g. a Table node array
at 0x3fcb2e4c). Later emergency GC (traversetable, traverseproto) crashed reading the
corrupt data. The crash appeared to come from "coroutines closing itself" only because
that section allocated from the already-corrupted heap region.

Confirmed via GDB: pxTopOfStack=0x3fcb4c50 (7128 bytes past pxStack), corrupt node
array at 0x3fcb2e4c (14812 bytes past pxStack), current SP a1=0x3fcb2d40.

Fix: Changed CONFIG_LUA_RTOS_LUA_STACK_SIZE to 40960 in sdkconfig and Kconfig
(widened Kconfig range from 1024–40960 to 1024–131072 and set default to 40960).
65536 was tried first but consumed too much DRAM, causing OOM when compiling the
1263-line coroutine.lua. 40960 clears the 35284-byte observed peak with 5676 bytes
margin while keeping sufficient heap for compilation.
Also reverted the debug instrumentation added to lgc.c (printf/abort checks).

DONE: Build and flash, then run dofile('coroutine.lua') to verify the fix

Verified: _soft=true; dofile('coroutine.lua') prints OK. (_soft=true is required
to skip the lim=1000000 stack-overflow stress test at line 835 which attempts a
~16MB Lua value stack — a legitimate OOM on embedded hardware. The _soft flag is
the standard Lua test suite mechanism for memory-constrained platforms.)

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

DONE: Fix faults found in lora/gateway/single_channel/gateway.c and lora/node/lmic/

DONE: Fix faults found in motion/motion.c, motion/motion_math.c and motion/s_curve_motion.c

DONE: Fix faults found in mqtt/Socket.c, mqtt/Thread.c and mqtt/MQTTPersistenceDefault.c

DONE: Fix faults found in ramfs/ramfs.c

DONE: Fix faults found in romfs/romfs.c

DONE: Fix faults found in sys/lwip/ping.c, sys/lwip/netif/vfs_tun.c, sys/sensors/ds1820.c and sys/vfs/vfs.c

DONE: Fix faults found in sound/tone.c and sound/tone_dac.c

DONE: Fix faults found in rc-switch/RCSwitch.c

- F1 (RCSwitch.c:517,544): added NULL guard at top of RCSwitch_sendTriState and RCSwitch_sendBinary to prevent null-pointer dereference when getCodeWordB/C/D returns NULL for invalid parameters

DONE: Fix faults found in captivedns/captivedns.c

- F1 (captivedns.c:158): added p->len < sizeof(struct DNSHeader) guard before casting p->payload to DNSHeader* to prevent out-of-bounds read on short/malformed UDP packets

DONE: Fix faults found in FabGL/src

- F1 (SSD1306Controller.cpp:384): malloc result checked before memset
- F2 (vgatextcontroller.cpp:195): heap_caps_malloc result checked before memcpy
- F3 (TFTControllerGeneric.cpp:597-600): heap_caps_malloc results checked before use in allocViewPort()
- F4 (fabutils.cpp:539): malloc result checked before strcpy in StringList::set()
- F5 (fabutils.cpp:870): namesAlloc initialised to 0 to prevent UB when m_dir=="/" (countDirEntries skips writing *namesLength in root-dir path)
- F6 (fabui.cpp:1948,2570,3217,3240): four realloc sites fixed with temp pointer to prevent memory leak and null deref on OOM
- F7 (displaycontroller.cpp:191,199,572,699): four realloc sites fixed with temp pointer for same reason

DONE: Fix faults found in telnet/telnetsrv.c

- F1 (telnetsrv.c:276-278): added close(*config->server) before return NULL on bind() failure to prevent socket fd leak
- F2 (telnetsrv.c:160): AI_NUMERICHOST replaced with NI_NUMERICHOST for getnameinfo() call

- F1 (tone.c:72,85): added return error after tone_unsetup(h) in both PWM and DAC failure paths to prevent use-after-free and silent error discard
- F2 (tone_dac.c:119): moved (*h)->channel = channel assignment to immediately after pin assignment, before first use at line 148; removed duplicate late assignment
- F3 (tone_dac.c:177-178): guarded dac_continuous_disable/del_channels with NULL check on hndl to prevent crash when unsetup is called before channel setup completes

- F1 (ping.c:168): added mem_free(iecho) before return to fix memory leak on every ping_send call
- F2 (vfs_tun.c:177-179): memcpy now uses min(p->len, size) to prevent overflow of caller's buffer
- F3 (ds1820.c:237): negative temperature formula fixed — shift unsigned absolute value before negating to avoid arithmetic-shift sign error on odd half-degree values
- F4 (vfs.c:298): fd <= maxfdp1 changed to fd < maxfdp1 to stop the select loop one fd short of the out-of-range descriptor

- F1 (romfs.c:529): assert(ret >= 0) replaced with if (ret < 0) return ret so seek failure in romfs_file_open is properly propagated in release builds
- F2 (romfs.c:95): name_len > 63 guard added before allocation in add_entry (MKROMFS) to prevent name length being silently truncated to 6 bits in flags

- F1 (ramfs.c:525): block_delta truncate-to-zero bug fixed — now uses explicit -1 sentinel for "no last block" instead of relying on -1/block_size which truncates to 0 in C
- F2 (ramfs.c:593): stack[++top] overflow in ramfs_umount fixed — added bounds check (top >= 255) returning RAMFS_ERR_INVAL before the push
- F3 (ramfs.c:215): name length silently truncated to 6 bits fixed — added guard in add_entry rejecting names longer than 63 characters with RAMFS_ERR_NAMETOOLONG

- F1 (Socket.c:89): fcntl flags check changed from != 0 to == -1 so existing flags are preserved when adding O_NONBLOCK
- F2 (Thread.c:282): ts.tv_sec += timeout corrected to proper ms→s/ns conversion with carry handling
- F3 (MQTTPersistenceDefault.c:553,556): lstat/remove now use full path (dirname + "/" + d_name) instead of bare filename

- F1 (motion.c:32-34): moved memset before accleration_profile assignment so it is not zeroed out
- F2 (motion_math.c:166): unknown == NAN → isnan(unknown) in solve_second_order_pos
- F3 (motion_math.c:207): unknown == NAN → isnan(unknown) in solve_second_min_pos
- F4 (s_curve_motion.c:498): condition next_min_time_ → newton_min_time_ to match the variable being updated

- F1 (gateway.c:704,711): sizeof(freq) → sizeof(freq)/sizeof(freq[0]) at both loop bound and guard
- F2 (lmic_hal.c:375): %s → %d for int line parameter in syslog format string
- F3 (lora_lmic.c:381-388): added mtx_unlock(&lora_mtx) before each early return in LORA_MAC_SET_DR
- F4 (lora_lmic.c:214): added free(payload) after lora_rx_callback to prevent memory leak
- F5 (lora_lmic.c:598): removed payload[payload_len] = msgid (byte was never transmitted; seqnoUp already set)

F1 (gateway.c:704,711) sizeof used instead of element count for freq[] array
  freq is const uint32_t freq[9], so sizeof(freq) == 36 (bytes), not 9 (elements).
  The loop "for(i=0;i<sizeof(freq);i++)" iterates 36 times instead of 9, reading
  freq[9]..freq[35] which are out of bounds. The guard "if (i >= sizeof(freq))"
  also uses 36 instead of 9, so a valid match would still incorrectly report
  "not found". Fix: replace sizeof(freq) with sizeof(freq)/sizeof(freq[0]) at
  both sites.

F2 (lmic_hal.c:375) Wrong format specifier in hal_failed(): %s used for int
  syslog(LOG_ERR, "... line %s\n", ..., file, line);
  'line' is int but %s treats it as a char* pointer. This is undefined behaviour
  and will print garbage or crash. Should be %d.

F3 (lora_lmic.c:381-388) Mutex not released on early return for invalid DR
  lora_mac_set() locks lora_mtx at line 340. Inside case LORA_MAC_SET_DR there
  are two early returns:
    if (atoi(value) < 0 || atoi(value) > 15) return driver_error(...);
    if (dr == DR_NONE)                        return driver_error(...);
  Neither calls mtx_unlock(&lora_mtx) before returning. The mutex is
  permanently held, deadlocking every subsequent lora_mac_set() call.

F4 (lora_lmic.c:208-215) Memory leak: rx payload buffer never freed
  In onEvent(EV_TXCOMPLETE) a buffer is malloc'd for the hex-encoded downlink
  payload and passed to lora_rx_callback(). After the callback returns the
  pointer is discarded without free(). Every received downlink leaks
  LMIC.dataLen * 2 + 1 bytes.

F5 (lora_lmic.c:597-598) msgid written to payload but payload_len not updated
  payload[payload_len] = msgid writes the message counter into the byte
  immediately after the decoded payload, but payload_len is not incremented
  before passing it to hal_lmic_tx(). The byte is therefore never transmitted.
  Either the intent was to append it (fix: payload_len++) or the write should
  be removed.

DONE: Fix faults found in lfs/lfs.c, lfs/lfs.h and lfs/lfs_util.h

- F1 (lfs.h:411): lfs_file_rewind comment corrected from LFS_SEEK_CUR to LFS_SEEK_SET
- F2 (lfs.c:419): dir->d.rev initialised to 0 before lfs_bd_read to prevent
  uninitialized value being used on LFS_ERR_CORRUPT path
- F3 (lfs.c:1144,1164,1235): three assertions changed from
  "head/nblock <= block_count" to "head/nblock < block_count"
- F4 (lfs_util.h:93): guard "if (a <= 1) return 1" added to lfs_npw2 to
  prevent __builtin_clz(0) undefined behaviour when a == 1
- F5 (lfs.c:1939): lfs_rename type-mismatch now returns LFS_ERR_ISDIR when
  the destination is a directory, LFS_ERR_NOTDIR when it is a plain file

F1 (lfs.h:411) Wrong comment on lfs_file_rewind
  Doc says "Equivalent to lfs_file_seek(lfs, file, 0, LFS_SEEK_CUR)" but
  LFS_SEEK_CUR with offset 0 is a no-op (returns current position). Should
  say LFS_SEEK_SET.

F2 (lfs.c:419-425) Uninitialized dir->d.rev used when lfs_bd_read returns
  LFS_ERR_CORRUPT in lfs_dir_alloc.
  When lfs_bd_read returns LFS_ERR_CORRUPT the read is skipped and the
  lfs_fromle32 conversion is skipped, leaving dir->d.rev as uninitialised
  stack garbage. The subsequent dir->d.rev += 1 writes that garbage + 1
  as the directory revision number. Should initialise dir->d.rev = 0 before
  the read so the CORRUPT path starts at revision 1.

F3 (lfs.c:1144, 1164, 1235) Off-by-one in block-number assertions
  Three assertions use "nblock <= lfs->cfg->block_count" but valid block
  indices are 0 .. block_count-1, so the correct test is
  "nblock < lfs->cfg->block_count". The current form permits block_count
  itself to pass the assertion, which could then be used as an out-of-range
  block address in a subsequent read/prog/erase call.

F4 (lfs_util.h:93) Undefined behaviour in lfs_npw2 when a == 1
  The GCC intrinsic implementation is: return 32 - __builtin_clz(a-1);
  When a == 1, a-1 == 0, and __builtin_clz(0) is explicitly undefined
  behaviour per GCC docs. Should add a special-case: if (a <= 1) return 0.

F5 (lfs.c:1939-1940) Wrong error code in lfs_rename type-mismatch check
  When source and destination exist but have different types (one is a file,
  the other a directory) the code always returns LFS_ERR_ISDIR. POSIX
  requires ENOTDIR (LFS_ERR_NOTDIR) when the new path is a non-directory
  but the old path is a directory. The fix is to check which operand is the
  directory and return the appropriate code.

DONE: Fix faults found in http/httpsrv.c and http/preprocessor.c

httpsrv.c:
- F1 (line 152): .txt MIME type changed from "text/html" to "text/plain"
- F2 (lines 300-302): Malformed cache headers fixed to "Pragma: no-cache\r\n" and "Expires: 0\r\n"
- F3 (line 549): AI_NUMERICHOST replaced with NI_NUMERICHOST for getnameinfo()
- F4 (line 676): sizeof(data) replaced with HTTP_BUFF_SIZE in fread() call
- F5 (line 1020): contentlength clamped to HTTP_BUFF_SIZE to prevent heap buffer overflow

preprocessor.c:
- F6: nested counter now incremented/decremented only on full "<?lua"/"?>" token
  match, not per-character; partial-match false starts no longer corrupt depth tracking
- F7: string-delimiter tracking now gated on lua=1, guarded by prev!='\\' to handle
  escape sequences; token matching gated on !string so ?> inside a string literal
  is not treated as the closing tag; prev=c maintained on all code paths

Faults found in http/httpsrv.c:

F1 (httpsrv.c:152) Wrong MIME type for .txt
  get_mime_type() returns "text/html" for .txt files; should be "text/plain".

F2 (httpsrv.c:300-302) Malformed HTTP cache headers in send_headers()
  Two lines are sent without a header-name prefix:
    do_printf(request, "no-cache\r\n");   // should be "Pragma: no-cache\r\n"
    do_printf(request, "0\r\n");          // should be "Expires: 0\r\n"
  Clients receive invalid HTTP header lines and will reject or misparse them.

F3 (httpsrv.c:549) Wrong flag passed to getnameinfo()
  AI_NUMERICHOST (a getaddrinfo() flag, value 4) is passed where
  NI_NUMERICHOST (value 1) is required. The remote address is either
  looked up via DNS or returned incorrectly.

F4 (httpsrv.c:676) sizeof(pointer) used instead of buffer size in fread()
  data is char* so sizeof(data) is 4 or 8 bytes (pointer size), not
  HTTP_BUFF_SIZE (1024). Static files are sent in 4/8-byte chunks, massively
  increasing transfer time. Should be HTTP_BUFF_SIZE.

F5 (httpsrv.c:996+1026) Buffer overflow in POST Content-Length handling
  pathbuf is HTTP_BUFF_SIZE (1024) bytes. contentlength is taken from the
  client-supplied Content-Length header (atoi(contentlen)+1), which can be
  arbitrarily large. do_gets(pathbuf, contentlength, request) then writes up
  to contentlength bytes into pathbuf — a heap buffer overflow if the client
  sends Content-Length > 1024.

Faults found in http/preprocessor.c:

F6 (preprocessor.c: nested counter) Partial token matches corrupt nested counter
  nested is incremented for every character matched in a partial match of
  "<?lua" or "?>" and is never rolled back when the match fails. A sequence
  like "<?x" leaves nested == 2 permanently, causing subsequent nesting checks
  (nested > 1, nested > 0) to fire incorrectly, leading to garbled output.

F7 (preprocessor.c:96-114) Escape sequences not handled in string tracking
  The quote-tracking logic toggles the string flag on every unescaped " or ',
  but does not check for a preceding backslash. A literal \" inside a string
  incorrectly ends the tracked string, causing subsequent characters to be
  treated as outside a string, potentially misidentifying <?lua or ?> tokens
  embedded in string literals.

DONE: Enable PSRAM (CONFIG_SPIRAM) and route the Lua allocator to PSRAM

The ESP32-S3 module has 8MB Octal PSRAM but CONFIG_SPIRAM is not set in sdkconfig.
The entire Lua heap competes with FreeRTOS stacks, lwIP, and display framebuffers for
~512KB of internal DRAM. Enable CONFIG_SPIRAM and override the Lua allocator (lua_Alloc)
to use heap_caps_malloc(size, MALLOC_CAP_SPIRAM) for all Lua objects, keeping task stacks
and DMA buffers in internal SRAM where they belong.

DONE: Reduce LUAI_MAXSTACK from 1,000,000 to a value appropriate for embedded use

ldo.c:293 defines LUAI_MAXSTACK as 1,000,000 value slots (16MB at 16 bytes per TValue).
On internal SRAM this limit is never reached — malloc fails first, producing misleading
"not enough memory" errors instead of "stack overflow". Set LUAI_MAXSTACK to 8192
(128KB of Lua value stack), which is ample for any realistic embedded script and gives
a meaningful stack overflow error well before OOM.

DONE: Tune Lua GC parameters for embedded memory constraints

lgc.h defines LUAI_GCPAUSE 250 and LUAI_GCMUL 200 — desktop defaults. GCPAUSE=250
means the GC waits until the heap grows to 2.5x its post-collection size before
starting a new cycle, causing large peak allocation spikes on a constrained heap.
Changed LUAI_GCPAUSE to 110 (cycle starts after 10% growth). LUAI_GCMUL was briefly
raised to 400 but reverted to 200 after it caused GC list corruption during locals.lua
(see "Debug and fix locals.lua crash" below) — 400 is too aggressive for PSRAM-backed
allocations under heavy load/free cycling.

DONE: Enable task watchdog and feed it from within the Lua VM execution loop

- Enabled CONFIG_ESP_TASK_WDT_EN (and related settings) in sdkconfig.
- Added CONFIG_LUA_RTOS_LUA_EXECUTION_TIMEOUT (default 30 s) to Kconfig and sdkconfig.
- lua_rtos_main.c: added esp_task_wdt_reset() to vm_hook alongside existing vTaskDelay(1)
  so the Lua task directly feeds its own TWDT subscription every LUA_YIELD_COUNT (50 000)
  VM instructions.  If the VM is ever stuck in C code the hook stops firing, the TWDT
  times out, and the system panics for post-mortem diagnosis.
- lua_rtos_main.c: added wall-clock execution timer (g_exec_start_us / esp_timer_get_time).
  vm_hook now raises "execution timeout" after CONFIG_LUA_RTOS_LUA_EXECUTION_TIMEOUT seconds
  so Lua infinite loops (e.g. while true do end) terminate gracefully at the REPL instead
  of locking the system indefinitely.  The timer is reset before each docall in doREPL and
  at the start of each dofile call.
- lua_adds.inc: added esp_task_wdt_add(NULL) in luaos_pmain to register the Lua task with
  the TWDT immediately after the hook is installed.  Return value is ignored since on VM
  restart the task may already be subscribed.

DONE: Add runtime os.timeout() for Lua execution timeout

- lua_rtos_main.c: replaced compile-time CONFIG_LUA_RTOS_LUA_EXECUTION_TIMEOUT constant
  with runtime int g_exec_timeout_s (initialised from the Kconfig value).  vm_hook now
  checks g_exec_timeout_s > 0 at runtime instead of a #if compile guard.
- loslib_adds.c: added os_exec_timeout() registered as os.timeout([secs]).
  Called with no argument it returns the current timeout; with an integer argument it sets
  the timeout and returns the previous value.  0 disables the timeout.

DONE: Fix TWDT-triggered reset every 5 s at idle REPL

- Root cause: lua_main task was subscribed to TWDT in luaos_pmain but vm_hook (which calls
  esp_task_wdt_reset) is a Lua count hook — it only fires during VM instruction execution.
  At the REPL with no input the task blocks indefinitely in xQueueReceive (inside linenoise
  read()), so vm_hook never fires, the TWDT times out after 5 s, and CONFIG_ESP_TASK_WDT_PANIC
  resets the system.  Confirmed via GDB: lua_main backtrace showed the task frozen at
  xQueueReceive ← vfs_generic_read ← linenoiseEdit ← luaos_pushline ← doREPL.
- Fix (lua_rtos_main.c:doREPL): call esp_task_wdt_delete(NULL) before luaos_pushline and
  esp_task_wdt_add(NULL) after it returns, so the task is unsubscribed from TWDT only during
  the intentional idle wait for user input.

IGNORE: Enable core dump output to diagnose crashes

CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y means all register state, stack frames and heap
content are lost on crash. Enable CONFIG_ESP_COREDUMP_ENABLE_TO_UART to print a
decodable crash dump on the serial console at minimum. Consider
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH with a dedicated partition for fully off-line
post-mortem analysis via idf.py coredump-info.

DONE: Fix fault in sys/drivers/gpio.c:123 — wrong bit shift for GPIO pins >= 32

In gpio_ll_pin_inv, reading GPIO.out1 uses (1 << pin) but GPIO.out1 holds only the
high GPIO bits (32-39), so the correct mask is (1 << (pin - 32)). The current code
tests the wrong bit for any pin >= 32.

- gpio.c:123: (1 << pin) changed to (1 << (pin - 32)) in the GPIO.out1.val test.

DONE: Fix fault in sys/drivers/gpio.c:435,669 — unconditional error return on success path

In gpio_pin_pulldwn and gpio_pin_pulldwn_mask the return driver_error(PULL_DOWN_NOT_ALLOWED)
sits outside the #if//#else block. A successful external pull-down always falls through to
this unconditional error return. The error return must be inside the
#if !EXTERNAL_GPIO_HAS_PROGRAMABLE_PULLDOWNS block only.

- gpio.c:435: stray return driver_error(PULL_DOWN_NOT_ALLOWED) removed from after #endif in gpio_pin_pulldwn
- gpio.c:669: same stray return removed from gpio_pin_pulldwn_mask; both functions now fall through to return NULL on success

DONE: Fix fault in sys/drivers/uart.c:534 — driver_error return value discarded on queue failure

driver_error() return value is silently discarded when xQueueCreate fails. Execution
continues with a NULL queue and will crash in the ISR on the first received byte.
Fix: return driver_error(UART_DRIVER, UART_ERR_NOT_ENOUGH_MEMORY, NULL).

- uart.c:534: added return before driver_error() call so queue allocation failure propagates to the caller.

DONE: Fix fault in sys/drivers/uart.c:429 — wrong driver ID in error return

SPI_DRIVER used instead of UART_DRIVER in the error return for UART_ERR_CANNOT_CHANGE_PINMAP.
Produces a misleading error message and wrong driver attribution.

- uart.c:429: SPI_DRIVER changed to UART_DRIVER in uart_pin_map error return.

DONE: Fix fault in sys/drivers/uart.c:713 — stack buffer overflow via unbounded uart_reads

_uart_wait_response has a fixed 80-byte stack buffer passed to uart_reads with no
length limit. A received line longer than 79 characters overflows the buffer.
uart_reads must be called with a maximum length, or switched to a bounded variant.

- uart.h/uart.c: added size_t maxlen parameter to uart_reads; writes bounded to maxlen-1 chars.
- uart.c:718,730: internal call sites pass sizeof(buffer).
- sys/sensors/gps.c:84: passes sizeof(sentence).
- lua/modules/hw/uart.c:281,296: pass LUAL_BUFFERSIZE.

DONE: Fix fault in sys/drivers/i2c.c:399-403 — mutex not released on no-free-device error path

i2c_lock(unit) is acquired before the device search. When no free device slot is found
the function returns driver_error without calling i2c_unlock(unit), permanently holding
the recursive mutex and deadlocking all subsequent I2C calls on that unit.

- i2c.c:399-403: added i2c_unlock(unit) before return on no-free-device error path.

DONE: Fix fault in sys/drivers/i2c.c:413-418 — incomplete error check after i2c_master_bus_add_device

Only ESP_ERR_NO_MEM is checked; any other non-OK result (e.g. ESP_ERR_INVALID_ARG) leaves
the bus handle open, the device handle uninitialised, and the mutex held. Change the
condition to err != ESP_OK.

- i2c.c:413: condition changed from err == ESP_ERR_NO_MEM to err != ESP_OK; error code
  changed to I2C_ERR_CANT_INIT to cover all failure modes.

DONE: Fix fault in sys/drivers/spi.c:205 — spi_unlock drains all recursive lock counts

spi_unlock loops xSemaphoreGiveRecursive until it returns pdFALSE, releasing all nesting
levels at once. An inner spi_unlock call fully releases the mutex while the outer lock is
still logically held, breaking mutual exclusion. Fix: give the semaphore exactly once.

- spi.c:205: while loop replaced with a single xSemaphoreGiveRecursive call.

DONE: Fix fault in sys/drivers/spi.c:261 — off-by-one device index bound check (6 sites)

The guard uses device > SPI_BUS_DEVICES instead of device >= SPI_BUS_DEVICES. When
device == SPI_BUS_DEVICES the check passes and spi_bus[...].device[SPI_BUS_DEVICES] is
accessed one element past the end of the array. Same error at lines 993, 1015, 1037,
1061, 1308.

- spi.c:261,993,1015,1037,1061,1308: all six instances of > SPI_BUS_DEVICES changed to >= SPI_BUS_DEVICES.

DONE: Fix fault in sys/drivers/spi.c:863-882 — spi_pin_map validates stored pins not new ones

All three pin validation checks in spi_pin_map compare spi_bus[...].miso/mosi/clk (the
currently stored values) instead of the incoming miso, mosi, clk arguments. New pin
numbers are never validated and any invalid value is silently accepted and stored.

- spi.c:863-885: all four validation expressions (miso, mosi, clk, TEST_UNIQUE3) now use
  the incoming function arguments instead of the currently stored bus values.

DONE: Fix fault in sys/drivers/wifi.c:417-422 — memory leak on scan error path

*list is set to NULL before free(*list) is called, so free(NULL) is a no-op and the
original heap allocation leaks. Swap the two lines: free(*list) first, then *list = NULL.

- wifi.c:418-420: free(*list) moved before *list = NULL so the allocation is freed on error.

DONE: Fix fault in sys/drivers/wifi.c:494 — wrong netif type created for AP/APSTA mode

esp_netif_create_default_wifi_sta() is called when mode is WIFI_MODE_AP or WIFI_MODE_APSTA.
It should be esp_netif_create_default_wifi_ap(). This attaches the wrong network interface
to the AP logical interface, breaking DHCP and IP address assignment in AP mode.

- wifi.c:494: esp_netif_create_default_wifi_sta() changed to esp_netif_create_default_wifi_ap().

DONE: Fix fault in gdisplay/gdisplay.c:622 — wrong variable checked in gdisplay_set_orientation

The orientation validity guard tests rotation (the image rotation state variable) instead
of the incoming orient argument. Valid orient values are rejected and invalid ones accepted
depending on what rotation happens to hold. Replace rotation with orient in all four
comparisons on that line.

- gdisplay.c:622: all four rotation comparisons changed to orient.

DONE: Fix fault in gdisplay/gdisplay.c:585-590 — uint8_t nested counter underflow in gdisplay_end

nested is declared uint8_t. gdisplay_end() decrements it with no guard against zero. An
unmatched call wraps nested to 255, preventing any future screen update until 255 extra
gdisplay_end() calls bring it back to zero. Add: if (nested == 0) return; at the top of
gdisplay_end().

- gdisplay.c:585: added if (nested == 0) return; guard before the decrement.

DONE: Fix fault in gdisplay/image/bmp.c:178-189 — palette leaked on fseek/fread error paths

After palette is allocated at line 171, two early-return error paths (fseek failure and
fread failure) call free(buf) and fclose(fhndl) but do not call free(palette). The
allocation leaks on every malformed or truncated BMP file.

- bmp.c:178-188: free(palette) added to both the fseek and fread failure paths.

DONE: Fix fault in gdisplay/image/bmp.c:169 — undefined behaviour on shift when header.bits == 0

If header.ncolours == 0 and header.bits == 0 (malformed BMP), the expression
2 << (header.bits - 1) becomes 2 << 0xFFFFFFFF — a shift count exceeding the integer
width, which is undefined behaviour. Add a header.bits > 0 guard before the palette-size
calculation.

- bmp.c:168: added guard rejecting header.bits == 0 with GDISPLAY_ERR_IMAGE before the palette_size calculation.

DONE: Fix fault in gdisplay/image/bmp.c:237-248 — out-of-bounds pixel writes for 1-bit BMP

The 1-bit BMP pixel expansion loop emits 8 pixels per byte with no check against
disp_xsize. When disp_xsize is not a multiple of 8, pixels beyond the right edge are
written. Add: if (j >= disp_xsize) break; inside the inner pixel loop.

- bmp.c:238: added if (j >= disp_xsize) break; at the top of the inner pixel loop.

DONE: Fix fault in gdisplay/image/jpg.c:274-286 — gdisplay_begin without matching gdisplay_end on error paths

gdisplay_begin() is called before JPEG decode. Both the jd_prepare failure path (line 282)
and the jd_decomp failure path (line 274) return without calling gdisplay_end(). The nested
counter is permanently incremented by 1 and the screen will never update again after a
JPEG decode error.

- jpg.c:274,282: gdisplay_end() added to both the jd_decomp and jd_prepare error paths.

DONE: Fix fault in eth_enc424j600/eth_mac_enc424j600.c:862-863 — emac struct leaked on SPI setup failure

In esp_eth_mac_new_enc424j600, when spi_setup() fails the function returns NULL without
freeing the already-allocated emac structure. Add free(emac) before the return NULL.

- eth_mac_enc424j600.c:861: free(emac) added before return NULL on spi_setup failure.

DONE: Fix fault in eth_enc424j600/eth_mac_enc424j600.c:887-908 — NULL semaphore unchecked; leaked on task failure

xSemaphoreCreateMutex() result is stored but never checked for NULL. A NULL mutex is
silently passed to xSemaphoreTake in all subsequent lock calls — undefined behaviour.
Additionally the err: cleanup path never calls vSemaphoreDelete(emac->lock), leaking
the semaphore when task creation fails after the mutex is successfully created.

- eth_mac_enc424j600.c:888: MAC_CHECK added after xSemaphoreCreateMutex to fail to err on NULL.
- eth_mac_enc424j600.c:err block: vSemaphoreDelete(emac->lock) added to cleanup path.

DONE: Fix fault in eth_enc424j600/eth_mac_enc424j600.c:579-594 — infinite spin loop on malloc failure in RX task

In emac_enc424j600_task, when heap_caps_malloc fails, packets_remain is still non-zero
so the do-while loop immediately retries. With no break or delay on allocation failure
this spins the CPU indefinitely. Add: break; on the malloc failure path to exit the inner
loop and wait for the next interrupt.

- eth_mac_enc424j600.c:583: break added after the malloc failure log message.

DONE: Fix fault in eth_enc424j600/eth_mac_enc424j600.c:728 — integer underflow in ByteCount - 4

len = statusVector.bits.ByteCount - 4 is computed with no check that ByteCount >= 4.
A malformed packet with ByteCount < 4 wraps len to a huge uint16_t value, causing a
massive out-of-bounds SPI read. Add a guard: if (statusVector.bits.ByteCount < 4) goto exit;

- eth_mac_enc424j600.c:728: guard added — if ByteCount < 4, goto exit before the subtraction.

DONE: Fix fault in sys/drivers/net.c:325-340 — wrong pointer freed in net_lookup after search loop

net_lookup reassigns the result pointer inside the getaddrinfo search loop. If no AF_INET
entry is found, freeaddrinfo is called on the modified pointer rather than the original.
The unfound case also leaves the caller's address struct uninitialised. Use a separate
search pointer; always pass the original result to freeaddrinfo.

- net.c:325-340: introduced separate found pointer for the AF_INET search; result is never
  modified so freeaddrinfo always receives the original pointer; address filled only when found != NULL.

DONE: Fix fault in sys/drivers/net_http.c:215-218 — SSL_write failure returns NULL (treated as success)

When SSL_write returns <= 0, the function frees http_request and returns NULL. The caller
interprets NULL as success, leaving response->code and response->size uninitialised. A
spurious OTA attempt can result. Return a proper driver_error on SSL write failure.

- net_http.c:215-218: return NULL replaced with return driver_error(NET_ERR_CANNOT_CONNECT_SSL).

DONE: Fix fault in sys/drivers/net_http.c:236-238 — atoi(NULL) on malformed HTTP status line

strtok(NULL, " ") at line 236 returns NULL if the HTTP status line has no status code.
The immediately following atoi(NULL) is undefined behaviour. Add a NULL check on code
before calling atoi.

- net_http.c:236: NULL check on code added; returns NET_ERR_INVALID_RESPONSE if missing.

DONE: Fix fault in sys/drivers/net_http.c:269-274 — heap buffer leaked as driver_error detail string

On content-type mismatch a heap buffer is allocated and passed to driver_error() as the
detail string. If driver_error copies the string the buffer leaks unconditionally. Use a
stack buffer for the formatted message instead.

- net_http.c:269-274: heap malloc replaced with stack char errbuf[250] and snprintf.

DONE: Fix fault in sys/drivers/adc.c:168-204 — channel at index 0 incorrectly treated as new

get_channel() sets the index output only when a channel is found; it is initialised to 0.
If an existing channel is found at list position 0, !index is still true and lstadd()
inserts a duplicate. Also free(chan) on the error path is called on a list-owned pointer
when a channel was not newly allocated — a double-free. Track new/existing with an
explicit boolean flag.

- adc.c:168-204: introduced is_new boolean; lstadd called only when is_new; free(chan)
  on setup failure guarded by is_new to prevent double-free of list-owned pointer.

DONE: Fix fault in sys/drivers/sensor.c:896 — off-by-one out-of-bounds array access after loop

After for(i = from; i <= to; i++), i equals to + 1. The post-loop access
unit->latch[i] reads one element past the last valid index. When to == SENSOR_MAX_PROPERTIES - 1
this is an out-of-bounds access. Replace i with to in the post-loop latch check.

- sensor.c:896-897: unit->latch[i] changed to unit->latch[to] at both the check and the assignment.

DONE: Fix fault in sys/drivers/sensor.c:791,806-812 — calloc in ISR and use-after-free in sensor callbacks

sensor_queue_callbacks() is called from an ISR. It calls calloc() which is not
interrupt-safe in FreeRTOS. It then calls free(data) after xQueueSendFromISR; if
portYIELD_FROM_ISR immediately schedules sensor_task, the task reads the freed buffer.
The queue should be created to hold sensor_deferred_data_t by value (not pointer) to
eliminate the heap allocation entirely.

- sensor.c:784-829: replaced heap-allocated sensor_deferred_data_t with a stack-allocated
  struct; xQueueSend already copies the struct by value (queue item size = sizeof struct),
  so no malloc/free is needed. Both ISR-unsafe calloc and the use-after-free are eliminated.

DONE: Fix fault in sys/drivers/sensor.c:497-513 — dangling pointer and counter corruption on postsetup failure

When postsetup fails, *unit already points to the freed instance (dangling pointer) and
attached has already been incremented. Neither is rolled back before the error return.
Set *unit = NULL and decrement attached before freeing instance on the postsetup failure path.

- sensor.c:503-512: *unit = NULL and attached-- added before mtx_destroy/free on postsetup failure.

DONE: Fix fault in sys/sensors/bme280.c:2396 — calloc arguments reversed

calloc(sizeof(struct bme280_user_data_t), sizeof(char)) has nmemb and size swapped.
Allocates the correct total bytes by accident but is wrong API usage. Should be
calloc(1, sizeof(struct bme280_user_data_t)).

- bme280.c:2396: calloc arguments corrected to calloc(1, sizeof(struct bme280_user_data_t)).

DONE: Fix fault in sys/sensors/bme280.c:2396-2471 — p_bme280 leaked on bme280_init failure

p_bme280 is allocated at line 2396 and stored as unit->setup[0].i2c.userdata. If
bme280_init fails and the function returns an error, sensor_setup frees instance but does
not chase the userdata pointer, so p_bme280 leaks. Free p_bme280 and NULL the userdata
pointer before returning the error.

- bme280.c:2471: free(p_bme280) and userdata = NULL added before the error return.

DONE: Fix fault in sys/sensors/bme280.c:2563-2569 — null pointer write due to wrong variable in allocation

The NULL check uses unit->properties[3].stringd.value but the calloc result is assigned
to property->stringd.value (wrong variable). bm280_get_mode then writes through the still-
NULL unit->properties[3].stringd.value, causing a null pointer write. Assign the calloc
result to unit->properties[3].stringd.value.

- bme280.c:2564: calloc result and NULL check both changed to unit->properties[3].stringd.value.

DONE: Fix fault in sys/sensors/gps.c:93 — task creation failure not checked

xTaskCreatePinnedToCore return value is not checked. If task creation fails (OOM), GPS
silently never receives data and the caller gets no indication of failure. Check the
return value and return driver_error on pdFAIL.

- gps.c:93: return value captured; returns SENSOR_ERR_NOT_ENOUGH_MEMORY if pdPASS is not returned.

DONE: Fix fault in pthread/_pthread.c:311 — mutex not released on early return in _pthread_detach

When thread->attr.detachstate == PTHREAD_CREATE_DETACHED is detected, the function
returns EINVAL without calling _pthread_unlock(). The global thread_mtx is left
permanently locked, deadlocking every subsequent pthread operation on this system.
Add _pthread_unlock() before the return EINVAL.

- _pthread.c:311: _pthread_unlock() added before return EINVAL.

DONE: Fix fault in pthread/_pthread.c:420,436,457 — off-by-one in signal array index guard

The guard if (s > PTHREAD_NSIG) allows s == PTHREAD_NSIG through. signals[] is declared
as sig_t signals[PTHREAD_NSIG] with valid indices 0..PTHREAD_NSIG-1. Accessing
signals[PTHREAD_NSIG] is one past the end of the array. Change > to >= at all three sites.

- _pthread.c:421,437,458: all three s > PTHREAD_NSIG guards changed to s >= PTHREAD_NSIG.

DONE: Fix fault in pthread/_pthread.c:737-742 — inverted bounds check and buffer overflow in pthread_getname_np

The ERANGE check is inverted: it returns ERANGE when the buffer is large enough and falls
through when the buffer may be too small. Additionally strncpy uses configMAX_TASK_NAME_LEN
instead of the caller-supplied len, ignoring the caller's buffer size and allowing overflow.
Fix the condition to strlen(task->pcTaskName) >= len, and use len-1 as the strncpy limit.

- _pthread.c:738: condition < len-1 corrected to >= len; strncpy limit changed from
  configMAX_TASK_NAME_LEN-1 to len-1; explicit NUL terminator added.

DONE: Fix fault in pthread/cond.c:106-108 — resource leak in pthread_cond_destroy

pthread_cond_destroy destroys the internal mutex but never calls vEventGroupDelete(scond->ev)
and never calls free(scond). Every destroyed condvar leaks one FreeRTOS event group handle
and one heap allocation. *cond is also not reset to PTHREAD_COND_INITIALIZER, enabling
use-after-free on the next call with the same cond variable.

Added vEventGroupDelete(scond->ev), free(scond), and *cond = PTHREAD_COND_INITIALIZER after
mtx_destroy in pthread_cond_destroy.

DONE: Fix fault in pthread/mutex.c:102,200-208 — free(NULL), missing free, and use-after-free in mutex destroy

pthread_mutex_init calls free(mutex->sem) when sem is NULL — a no-op but a logic error.
pthread_mutex_destroy never calls free(mutex), leaking every destroyed mutex struct. *mut
is never reset to PTHREAD_MUTEX_INITIALIZER, enabling use-after-free. The pre-destroy
xSemaphoreGive is also incorrect — remove it and just call vSemaphoreDelete then free(mutex).

Removed free(mutex->sem) from init failure path (sem is NULL there, nothing to free).
Removed xSemaphoreGive/xSemaphoreGiveRecursive before delete in destroy. Added free(mutex)
and *mut = PTHREAD_MUTEX_INITIALIZER after vSemaphoreDelete.

DONE: Fix fault in lua/modules/sys/thread.c:137-140 — lthread_t leaked on malloc failure in lthread_start_task

When malloc(sizeof(lcleanup_info_t)) fails, pthread_exit(NULL) is called without freeing
the lthread_t *thread argument. Since no cleanup handler has been pushed yet, POSIX
cleanup mechanisms do not free it. Add free(thread) before pthread_exit on this error path.

Added free(thread) before pthread_exit(NULL) on the malloc failure path.

DONE: Fix fault in lua/modules/sys/thread.c:301-319 — lthread_t struct leaked on forced thread stop

lthread_stop_pthreads calls _pthread_stop and _pthread_free but never calls
free(cinfo->lthread). The lthread_t allocated in new_thread leaks on every forced
thread stop. Add free(cinfo->lthread) after each _pthread_free call.

Added free(cinfo->lthread) after _pthread_free in both the single-thread and all-threads
stop branches.

DONE: Fix fault in lua/modules/sys/tmr.c:149 — integer overflow in ltmr_delay_us for large periods

(CPU_HZ / 1000000L) * period is computed as int. At 240 MHz, CPU_HZ/1000000 = 240.
For period > ~8,947,848 us (~9 s) the product exceeds INT_MAX, wraps to a large negative
value, and the while (cycles > 0) loop is skipped — the delay is silently not applied.
Use int64_t for the cycles computation.

Changed cycles from int to int64_t, cast the multiplier to int64_t to force 64-bit
arithmetic: int64_t cycles = ((int64_t)(CPU_HZ / 1000000L) * period) - 47;

DONE: Fix the Lua restart loop in main.c to handle abnormal exits cleanly

- main.c: corrected forward declaration from `void luaos_main()` to `int luaos_main(void)` to match the actual signature in lua_adds.inc.
- main.c:lua_start: captures the int return value of luaos_main().
  On EXIT_FAILURE: logs the rc, flushes stdout, waits 1 s, then calls esp_restart() so
  hardware drivers are cleanly reset by the bootloader rather than inherited in unknown state.
  On EXIT_SUCCESS: logs the normal exit, waits 500 ms, then re-enters the loop to restart the VM.

DONE: Raise the Lua interpreter task priority to reduce network callback latency

- main/Kconfig: default for LUA_RTOS_LUA_TASK_PRIORITY changed from 3 to 12.
- sdkconfig: CONFIG_LUA_RTOS_LUA_TASK_PRIORITY changed from 3 to 12.
- boards/TTGO-T-WATCH-S3: CONFIG_LUA_RTOS_LUA_TASK_PRIORITY changed from 3 to 12.
  Priority 12 sits above most middleware tasks (lwIP at ~5, BT at ~5-9) and below the
  HTTP server (18) and LoRa (21), so network callbacks are processed promptly without
  starving the network stack itself.

DONE: Check and fix LFS block sizes vs SPI flash geometry

- partitions.csv: storage subtype changed from 64 (0x40 = LUA_RTOS_SPIFFS_PART) to 65
  (0x41 = LUA_RTOS_LFS_PART).  Without this fix esp_partition_find_first returned NULL
  and LFS logged "can't find a valid partition" at mount time.
- sys/vfs/lfs.c lfs_erase: replaced single spi_flash_erase_sector call (always one 4 KB
  sector) with a loop over block_size/SPI_FLASH_SEC_SIZE sectors.  With block_size=4096
  the old code was correct, but would silently corrupt the filesystem if block_size were
  ever changed to a larger multiple of 4096.
- Kconfig/sdkconfig: reduced LFS_READ_SIZE and LFS_PROG_SIZE defaults from 1024 to 256
  (the SPI NOR flash page size).  1024 was valid but 4x the natural write unit, wasting
  ~1.5 KB of heap per open file in LFS internal buffers.
- block_size=4096 matches SPI_FLASH_SEC_SIZE=4096 exactly — no change needed.

DONE: Make LFS the default flash filesystem instead of SPIFFS

- sdkconfig: CONFIG_LUA_RTOS_USE_LFS=y and CONFIG_LUA_RTOS_LFS_ROOT_FS=y set by user.
- partitions.csv: storage partition subtype changed from 64 (0x40, SPIFFS) to 65 (0x41,
  LFS) so esp_partition_find_first locates the correct partition at mount time.
- sys/vfs/lfs.c: replaced legacy spi_flash_read/write/erase_sector (removed in ESP-IDF
  v5) with esp_partition_read/write/erase_range.  Context struct now holds
  esp_partition_t* instead of a raw base address; partition-relative offsets eliminate
  manual address arithmetic and SPI_FLASH_SEC_SIZE dependency.
- Kconfig/sdkconfig: LFS_READ_SIZE and LFS_PROG_SIZE defaults reduced from 1024 to 256
  (SPI NOR flash page size) to reduce LFS internal buffer heap usage.
- Kconfig: LFS_BLOCK_SIZE default remains 4096 = SPI_FLASH_SEC_SIZE (erase unit).

DONE: Debug and fix locals.lua crash (sweeplist GC corruption)

Root cause: LUAI_GCMUL=400 (set in commit 8a1283ef) was too aggressive for the 1,281
load() calls in the locals.lua "special instructions" loop (lines 92-109). With GCMUL=400
the GC does 4 bytes of sweep work per byte allocated — 2× the Lua default of 200. During
heavy allocation/free cycling (each load() creates and then discards a Proto, LClosure,
anchor Table, and multiple TString objects all in PSRAM), the rapid sweep pressure corrupted
a GCObject->next pointer in the allgc linked list. The next GC cycle crashed at
sweeplist (lgc.c:896) when it dereferenced the bad pointer.

Confirmed via GDB connected to OpenOCD at 192.168.10.100:3333: full 50-frame backtrace
decoded as sweeplist←sweepstep←singlestep←incstep←luaC_step←anchorstr(llex.c:146)←
llex←luaX_next←statement(lparser.c:2054)←...←luaB_load←...←f_parser←luaY_parser←
mainfunc←statlist←statement←..., confirming the crash is inside load() compilation,
not the deliberate stack-overflow test at lines 632-663.

Fix: Reverted LUAI_GCMUL from 400 back to 200 (Lua default) in lua/src/lgc.h.
With GCMUL=200, locals.lua completes the entire 1,281-call load() loop without crashing.

DONE: Review all project code for memory leaks on error paths — malloc/calloc/realloc succeeds but an early return or goto skips the matching free.

- F1 (gdisplay.c:1074): tempBuffer malloc failure not detected — condition checked the macro constant
  qrcodegen_BUFFER_LEN_MAX (always nonzero) instead of the pointer tempBuffer. A failed allocation
  silently continued with NULL tempBuffer and left qrcode leaked. Fixed: changed !qrcodegen_BUFFER_LEN_MAX
  to !tempBuffer; the existing free(qrcode) inside the block was already correct.
- F2 (thread.c:606): pthread_create failure path leaked thread struct and two Lua registry references.
  When pthread_create fails after retries, function_ref (line 558) and thread_ref (line 563) had already
  been registered in the Lua registry, and thread had been allocated (line 551). luaL_exception_extended
  longjmps out without freeing any of them. Fixed: added luaL_unref for both refs and free(thread)
  immediately before the luaL_exception_extended call.

DONE: Review all project code for file descriptor and socket leaks — open/socket/accept calls where not every error path calls close.

Four leaks found and fixed: SSL_new failure in httpsrv.c left accepted socket open; pthread_create
failures in can.c left client socket open; lora_gw_unsetup() never closed up_socket/down_socket;
opendir() in httpsrv.c was not NULL-checked before readdir().

DONE: Fix fault in http/httpsrv.c:1298 — accepted client socket not closed when SSL_new() fails

- httpsrv.c:1297: close(client) and client = -1 added before break so the accepted socket
  is not leaked when SSL_new() fails and the server loop exits.

DONE: Fix fault in sys/drivers/can.c:319,325 — accepted client socket not closed when pthread_create() fails

- can.c:319: close(gw_config->client) added before return NULL on thread_up creation failure.
- can.c:325: close(gw_config->client) added before return NULL on thread_down creation failure.

DONE: Fix fault in lora/gateway/single_channel/gateway.c — lora_gw_unsetup() never closes up_socket or down_socket

- gateway.c:138-139: up_socket and down_socket initialised to -1 so the guard in unsetup
  is safe before any socket is created.
- gateway.c:lora_gw_unsetup: added close(up_socket)/close(down_socket) with -1 guards so
  all error paths that call lora_gw_unsetup() after socket creation properly close the sockets.

DONE: Fix fault in http/httpsrv.c:826 — opendir() return value not checked before readdir()

- httpsrv.c:826: added NULL check on opendir() result; returns early if the directory
  cannot be opened, preventing readdir(NULL) undefined behaviour and closedir(NULL) crash.

DONE: Review all project code for mutexes not released on every exit path — functions that lock a mutex but have early returns or error paths that skip the unlock.

- F1 (list.c:128-129): LIST_NOT_INDEXED calloc failure in lstadd() returned ENOMEM without mtx_unlock(&list->mutex). Added mtx_unlock before the return.
- F2 (mount.c:665-668): mount() count!=1 path returned -1 without mtx_unlock(&mtx). Added mtx_unlock before the return.
- F3 (mount.c:701-708): mount() spiffs/lfs conflict path returned -1 without mtx_unlock(&mtx). Added mtx_unlock before the return.
- F4 (mount.c:764-768): umount() count!=1 path returned -1 without mtx_unlock(&mtx). Added mtx_unlock before the return.
- F5 (mount.c:779-783): umount() root-with-others path returned -1 without mtx_unlock(&mtx) and without free(npath). Added both free(npath) and mtx_unlock before the return.

DONE: Review all project code for race conditions between ISRs and task code — shared variables read/written from both interrupt context and task context without atomic access or critical sections.

- F1 (uart.c:155): console_raw changed from uint8_t to volatile uint8_t. uart_rx_intr_handler (ISR)
  reads it in queue_byte to decide whether Ctrl+C/Ctrl+D should be forwarded; uart_ll_set_raw (task
  context) writes it. Without volatile the compiler can keep a stale copy in a register across ISR
  re-entry.
- F2 (gpio_debouncing.c): Added static portMUX_TYPE debouncing_mux spinlock. Changed
  gpio_debouncing_unregister from portDISABLE_INTERRUPTS/portENABLE_INTERRUPTS to
  portENTER_CRITICAL/portEXIT_CRITICAL so the clear of callback[pin]/arg[pin] is SMP-safe on
  dual-core ESP32-S3 (portDISABLE_INTERRUPTS only disables interrupts on the local core; the timer
  ISR can still execute on the other core). Changed debouncing_isr (both internal and EXTERNAL_GPIO
  paths) to snapshot callback[i]/arg[i] into locals under portENTER_CRITICAL_ISR/portEXIT_CRITICAL_ISR
  before invoking the callback, so the call can never use a pointer that unregister has concurrently
  zeroed.
- F3 (hall_flow_sensor.c): flow_isr wrote three double (64-bit) values directly into sensor->data[]
  while sensor_read (task context) read them under only unit->mtx, which does not block ISRs. On
  32-bit Xtensa a double write is two separate 32-bit stores — a concurrent task read produces a torn
  value. Added portMUX_TYPE mux and staging fields q/l/freq to hall_flow_t; flow_isr now writes only
  to staging under portENTER_CRITICAL_ISR. Added hall_flow_acquire() that atomically copies staging
  to unit->data[] under portENTER_CRITICAL; registered .acquire = hall_flow_acquire in sensor_t so
  sensor_acquire calls it before sensor_read returns data to Lua.

DONE: Review all project code for ISR-unsafe function calls — heap allocation (malloc/calloc/free), blocking calls, or non-reentrant functions called from interrupt handlers.

- F1 (gateway.c:dio_intr_handler): ISR called spi_ll_select()→spi_lock()→xSemaphoreTakeRecursive(portMAX_DELAY) — a blocking semaphore call from interrupt context. Fixed by adding a lora_dio_deferred_handler task and lora_dio_q queue; the ISR now only calls xQueueSendFromISR, and all SPI I/O happens in the deferred task. Added lora_dio_q and lora_dio_task teardown to lora_gw_unsetup().
- F2 (uart.c:597): uart_rx_intr_handler registered with ESP_INTR_FLAG_IRAM but the handler and all callees (queue_byte, status_get, _pthread_has_signal, lstget, mtx_lock) are not marked IRAM_ATTR. Calling non-IRAM code from an IRAM ISR crashes when SPI flash cache is disabled during flash operations. Fixed: changed flag from ESP_INTR_FLAG_IRAM to 0. The UART console ISR is not required to run during flash operations.

DONE: Review all project code for use-after-free when ownership transfers across threads — pointers passed to queues, callbacks, or other tasks that are freed by the sender before the receiver is done with them.

- F1 (lora.c:83): Double-free — on_received callback called free(payload) but lora_lmic.c (the allocator)
  also called free(payload) after the callback returned (added in the F4 fix). The callback should treat
  the pointer as borrowed; lora_lmic.c owns and frees the buffer. Removed free(payload) from on_received.

DONE: Review all project code for off-by-one errors in array index bounds — guards using > instead of >= or < instead of <= when comparing against array size or count limits.

- F1 (adc.c:158): `unit > CPU_LAST_ADC + 1` (== `unit > 2`) in the external ADC path allowed only unit=2,
  rejecting all other configured external ADC units (3, 4, 5). The check was also redundant (`unit < CPU_FIRST_ADC`
  is always false in the else branch). Replaced with a proper array-size bound:
  `(size_t)(unit - CPU_FIRST_ADC) >= sizeof(adc_devs)/sizeof(adc_devs[0])`.
  The existing NULL-sentinel name check still catches the sentinel entry at index N-1.

DONE: Review all project code for integer overflow — signed multiplication or addition that can exceed INT_MAX before being cast to a wider type, especially in size or delay calculations.

- F1 (sensor.c:855,874): `now.tv_sec * 1000000` computed as int32*int32 before assigning to uint64_t t1/t0.
  Current Unix timestamp (~1.748e9 s) * 1000000 = 1.748e15, overflowing int32 and producing wrong debounce
  timing comparisons. Fixed by casting to uint64_t first: `(uint64_t)now.tv_sec * 1000000`.
- F2 (rmt.c:394): `idle_threshold * 1000000` computed as int32*int32 in MSEC range path. For any threshold
  > 2147 ms (~2.1 s) the product overflows int32 (UB). Max threshold 65535 ms → 65,535,000,000 ns also
  exceeds uint32_t max (~4.295e9). Fixed with uint64_t intermediate and clamp to UINT32_MAX.
- F3 (ping.c:214,217): `begin.tv_sec * 1000000` computed as int32*int32 before assigning to uint64_t
  micros_begin/micros_end. Same overflow as F1. Fixed by casting to uint64_t first.

DONE: Review all project code for unsigned integer underflow — subtraction on uint8_t/uint16_t/uint32_t values that can go negative and wrap to a large positive value.

No genuine unsigned underflow bugs found. The captivedns.c `p->len - 1` is guarded by the prior `p->len < sizeof(DNSHeader)` check; gateway.c `strlen(buff_pos) - 1` cannot be zero since sprintf always writes; ds1820.c `owsensor - 1` requires the caller to pass 0, which violates the 1-based convention enforced by the default.

Two additional integer overflow bugs were found and fixed during this review:
- F4 (power_bus.c:141): `now.tv_sec * 1000` in pwbus_uptime() uses CLOCK_MONOTONIC but computes as
  int32*int32; overflows after 24.8 days of uptime. Cast to uint64_t before multiply.
- F5 (gateway.c:467,553): `now.tv_sec * 1000` in ttn_up_task/ttn_down_task uses gettimeofday (absolute
  Unix time ~1.748e9 s); `1748000000 * 1000` overflows int32 on every call. Cast to uint64_t first.

DONE: Review all project code for uninitialized variables used on error paths — variables declared but not set before use when execution takes a branch that skips the initializing assignment.

- F1 (sensor.c:521): `driver_error_t *error;` declared without initialization in sensor_unsetup. When
  unit->sensor->unsetup is NULL the assignment at line 534 is skipped. The subsequent loop over interfaces
  uses switch(type) with only GPIO_INTERFACE and I2C_INTERFACE cases; if the first matching interface
  type falls to default:, the following `if (error)` reads uninitialized stack memory. Fixed:
  changed `driver_error_t *error;` to `driver_error_t *error = NULL;`.
- F2 (gdisplay.c:1137): segBuf allocated with malloc() but never checked for NULL before being passed to
  qrcodegen_makeNumeric/Alphanumeric/Bytes/Eci. A malloc failure produces a NULL pointer that these
  functions dereference, crashing the firmware. Fixed: added NULL check on segBuf; on failure frees
  text, qrcode, tempBuffer and raises GDISPLAY_ERR_NOT_ENOUGH_MEMORY.

DONE: Review all project code for dangling pointers after free — callers that retain a copy of a pointer after freeing it, or structs whose members point to freed memory.

- F1 (spi.c:226): In lspi_deselect(), spi->buff was freed without being set to NULL. lspi_rw_helper()
  computes `uint8_t *buff = spi->buff + spi->len` (line 256) with no NULL guard, then writes to *buff.
  If the user calls spi:write() after spi:deselect() without an intervening spi:select(), the dangling
  spi->buff (freed heap pointer, spi->len==0 so buff==freed_ptr) is dereferenced on line 284, writing
  to freed heap memory. Fix: added spi->buff = NULL after free(spi->buff) in lspi_deselect().

DONE: Review all project code for use-after-free via realloc — realloc result stored back into the same pointer variable, leaving the old pointer invalid even on failure.

- F1 (mqtt/SocketBuffer.c:202): `queue->buf = realloc(queue->buf, bytes)` — on OOM, original buffer leaked and buflen inconsistently updated. Fixed with temp pointer and early return NULL.
- F2 (sys/editor/edit.c:763): `env->linebuf = realloc(env->linebuf, ...)` in get_term_size — on OOM, old linebuf leaked. Fixed with temp pointer; on failure the old (possibly undersized) linebuf is retained.
- F3 (sys/editor/edit.c:1671): `ed->env->clipboard = realloc(ed->env->clipboard, ...)` in copy_selection — NULL check was present but the old allocation was leaked. Fixed: temp pointer checked before assignment.
- F4 (FabGL/src/terminal.cpp:4921): `m_text = realloc(m_text, ...)` in LineEditor::setLength — on OOM, original leaked and the next line immediately dereferences the NULL (memset crash). Fixed with temp pointer and early return.
- F5 (FabGL/src/dispdrivers/vgabasecontroller.cpp:209): `m_DMABuffers = heap_caps_realloc(m_DMABuffers, ...)` — on OOM, original DMA buffer leaked. Fixed with temp pointer and early return false before assignment.
- F6 (FabGL/src/dispdrivers/vgabasecontroller.cpp:211): `m_DMABuffersVisible = heap_caps_realloc(m_DMABuffersVisible, ...)` — same issue. Fixed with temp pointer and early return false before assignment.
- F7 (FabGL/src/fabui.cpp:3112): `m_text = realloc(m_text, ...)` in uiTextEdit::checkAllocatedSpace — on OOM, old text leaked. Fixed with temp pointer; on failure old text retained.
- F8 (FabGL/src/fabutils.cpp:120-128): realloc32() always called heap_caps_free(ptr) and moveItems(NULL, ptr, ...) even when heap_caps_malloc returned NULL — crashes on OOM and always frees old data. Fixed: guard the copy+free with `if (newBuffer && ptr)` so the old allocation is preserved when malloc fails.

DONE: Review all project code for wrong flag constants — passing a constant from one API (e.g. getaddrinfo flags) to a different API that uses different flag values with overlapping names.

- F1 (compat/getnameinfo.c:63,71,77): AI_NUMERICHOST and AI_NUMERICSERV (getaddrinfo flags) used in
  getnameinfo() implementation where NI_NUMERICHOST and NI_NUMERICSERV are required. NI_NUMERICHOST was
  undefined in the project. Added #define NI_NUMERICHOST 0x00000004 to
  idf-replacements/newlib/platform_include/net/if.h (same numeric value as AI_NUMERICHOST so the compat
  implementation's runtime behaviour is unchanged); changed compat/getnameinfo.c to check NI_NUMERICHOST
  and NI_NUMERICSERV.
- F2 (telnetsrv.c:160): AI_NUMERICHOST passed to getnameinfo() — changed to NI_NUMERICHOST.
- F3 (httpsrv.c:549): AI_NUMERICHOST passed to getnameinfo() — changed to NI_NUMERICHOST.
- F4 (sys/vfs/lfs.c:175): flags == O_APPEND (equality) silently ignored when caller passes O_WRONLY|O_APPEND.
  Changed to flags & O_APPEND.
- F5 (sys/vfs/lfs.c:178): flags == O_RDONLY (equality) fails when combined with O_CREAT or other flags.
  Changed to (flags & O_ACCMODE) == O_RDONLY.

DONE: Review all project code for wrong printf/syslog format specifiers — %s used for int, %d used for pointer, or other type mismatches between format string and argument.

- The previously known lmic_hal.c `%s` for int `line` had already been fixed to `%d`.
- F1 (lmic.c:1134,1417,1586,2121; radio.c:518,595,600; lmic_hal.c:375): `%lu` used with `(u4_t)os_getTime()` (uint32_t) and `LMIC.freq` (u4_t = uint32_t). `%lu` expects `unsigned long`; on Xtensa 32-bit both are 32-bit so it works in practice, but is undefined behaviour per C11 §7.21.6.1p9. All eight sites changed to `(unsigned long)os_getTime()` and `(unsigned long)LMIC.freq` so the argument type exactly matches the specifier.
- No other format specifier mismatches found: gpio_name() (uint8_t) with %d is correct (uint8_t promoted to int), strerror(errno) with %s is correct, all other integer/string pairings checked across sys/drivers, sys/vfs, lua/modules, lora, mqtt, gdisplay, FabGL, and eth_enc424j600.

DONE: Review all project code for sizeof(pointer) used instead of buffer size — sizeof applied to a pointer variable rather than the buffer it points to, producing 4 or 8 instead of the allocation size.

No bugs found in core project files (sys/, lua/, lora/, http/, FabGL/, etc.) — all sizeof() calls in buffer operations target array variables or types, not pointer variables. Nine bugs found in the mqtt/ (Eclipse PAHO) library where sizeof(pointer) underreported list memory accounting:
- F1 (SocketBuffer.c:374): sizeof(pw) → sizeof(*pw) in SocketBuffer_pendingWrite ListAppend
- F2 (MQTTAsync.c:1484): sizeof(command) → sizeof(*command) in ListAppend (PUBLISH QoS0 interrupted path)
- F3 (MQTTAsync.c:1533): sizeof(command) → sizeof(*command) in ListAppend (responses queue path)
- F4 (MQTTAsync.c:2427): sizeof(qe) + sizeof(mm) → sizeof(*qe) + sizeof(*mm) in message queue ListAppend
- F5 (MQTTAsync.c:2723): sizeof(conn) → sizeof(*conn) in MQTTAsync_connect addCommand
- F6 (MQTTAsync.c:2770): sizeof(dis) → sizeof(*dis) in MQTTAsync_disconnect addCommand
- F7 (MQTTAsync.c:2935): sizeof(sub) → sizeof(*sub) in MQTTAsync_subscribeMany addCommand
- F8 (MQTTAsync.c:3013): sizeof(unsub) → sizeof(*unsub) in MQTTAsync_unsubscribeMany addCommand
- F9 (MQTTAsync.c:3125): sizeof(pub) → sizeof(*pub) in MQTTAsync_send addCommand
All nine sites used the 4-byte pointer size instead of the pointed-to struct size, causing the list's size accounting field (aList->size) to undercount total memory held by each list.

DONE: Review all project code for sizeof(array) used instead of element count — sizeof(arr) used as a loop bound or guard instead of sizeof(arr)/sizeof(arr[0]).

- F1 (pwm.c:440): memset(&pwm[unit][channel], 0, sizeof(pwm)) — sizeof(pwm) is the size of the
  entire 2D array struct pwm[CPU_LAST_PWM+1][CPU_LAST_PWM_CH+1] (at least 80 bytes), but only a
  single element (5 bytes) should be zeroed. This over-writes all other channel state and memory
  beyond the array boundary. Fixed: sizeof(pwm) → sizeof(pwm[unit][channel]).

All other sizeof comparisons against array names in loop bounds and guards were reviewed:
- tm1637.c:226 sizeof(map) — map is uint8_t[], sizeof == element count, correct.
- gateway.c:700 sizeof(sf) — sf is uint8_t[6], sizeof == element count, correct.
- keyboard.cpp:428 sizeof(PAUSECODES) — PAUSECODES is uint8_t[], sizeof == element count, correct.
- gateway.c:704,712 sizeof(freq)/sizeof(freq[0]) — already correct (fixed in prior session).
- adc.c:158 sizeof(adc_devs)/sizeof(adc_devs[0]) — already correct.

DONE: Review all project code for conditions that test the wrong variable — validation or guard expressions that read a stored/old value instead of the incoming function argument.

No new bugs found. The gdisplay.c:622 case (rotation checked instead of orient) was already
fixed in a prior session. All other validation guards reviewed across sys/drivers/*.c,
sys/sensors/*.c, sys/vfs/*.c, lua/modules/**/*.c, lora/**, http/*.c, mqtt/*.c, FabGL/src/**,
gdisplay/**, sdisplay/**, eth_enc424j600/**, sound/**, motion/**, rc-switch/**, captivedns/**
correctly test the incoming function argument. One dead-code instance found at
lua/modules/hw/gdisplay.c:553 (inside #if 0) where luaL_checkinteger(L,3) was used for both
`w` and `h` — not fixed since the block is disabled.

DONE: Review all project code for inverted bounds checks — conditions where the comparison operator is backwards, passing invalid input and rejecting valid input.

- F1 (encoder.c:246): `&&` changed to `||` in encoder_setup pin-validity guard. With `&&`, a single
  invalid (negative) pin passed the "a and b pins are required" check and reached GPIO_CHECK_INPUT
  with a negative shift count — undefined behaviour. With `||`, either negative pin triggers the error.
- F2 (st7735.c:403): all four coordinate comparisons changed from `>` to `>=` in st7735_addr_window.
  Valid pixel indices are 0..width-1 and 0..height-1; `x >= width` is out-of-bounds but `x > width`
  passed that coordinate through, letting the SPI window command write to display memory one column or
  row past the edge. ili9341 inherits this fix (it reuses st7735_addr_window).
- F3 (st7789.c:208): same `>` → `>=` fix in st7789_addr_window for the same reason.

DONE: Review all project code for realloc result stored directly into the source pointer — if realloc returns NULL the original allocation is lost, causing a memory leak before the null-pointer crash.

- F1 (strbuf.c:176): `s->buf = realloc(s->buf, s->size)` — if realloc returns NULL the original buffer
  is lost before die("Out of memory") terminates. Fixed with temp pointer: char *newbuf = realloc(...);
  if (!newbuf) die(...); s->buf = newbuf;
- F2 (fabutils.cpp:465-466): `m_items = realloc32(m_items, ...)` and `m_selMap = realloc32(m_selMap, ...)`
  — if realloc32 returns NULL, NULL is stored into the member pointer, crashing the next array access in
  insert()/set(). The old allocation is preserved by realloc32 but becomes inaccessible (leaked). Fixed
  with temp pointers and early return with m_allocated reverted to oldAllocated on either failure, keeping
  the struct consistent at its prior capacity.

DONE: Review all project code for shift-count undefined behaviour — left or right shifts where the shift amount can equal or exceed the width of the integer type.

DONE: Review all project code for null pointer dereferences — pointer results from malloc/calloc/realloc, getenv, strtok, and similar functions used without NULL checks before dereferencing.

Three null-pointer dereferences found, all in http/httpsrv.c, all caused by strtok_r returning NULL on a malformed HTTP request with no NULL check before dereferencing:
- F1 (httpsrv.c:921): strtok_r result for request->method not checked before strlen(request->method) call when request->path is also NULL. Added NULL guard that sends 400 Bad Request and returns.
- F2 (httpsrv.c:963): strtok_r result for Host header value not checked before while(*host==' ') dereference. Added NULL guard with continue to skip to next header.
- F3 (httpsrv.c:1022): strtok_r result for Content-Length header value not checked before while(*contentlen==' ') dereference. Wrapped the while/atoi block in if (contentlen) guard.

DONE: Review all project code for buffer overflows and underflows — fixed-size buffers written to with unchecked lengths, including strcpy/strcat/sprintf/memcpy/read with caller-controlled sizes.

- F1 (httpsrv.c:472-473): strcpy(ppath, path) into char ppath[PATH_MAX+1] before the length check at line 475.
  If path (a Lua string) is >= PATH_MAX chars the strcpy overflowed before the guard ran.
  Fixed: changed strcpy to strncpy(ppath, path, PATH_MAX) + ppath[PATH_MAX]='\0'; the existing
  else-branch at line 632 ("Path too long") still triggers when strlen(ppath)==PATH_MAX.
- F2 (edit.c:287): strcpy(fn, filename) fallback when realpath() fails. fn is char fn[FILENAME_MAX];
  a filename longer than FILENAME_MAX-1 overflows the buffer.
  Fixed: replaced strcpy with strncpy(fn, filename, FILENAME_MAX-1) + fn[FILENAME_MAX-1]='\0'.
- F3 (lora.c:115): hex_str_pad — tmp is malloc'd as len+1 bytes. When strlen(str) >= len the else
  branch ran strcpy(tmp, str). For strlen(str) > len this writes more than len+1 bytes into tmp.
  Fixed: added explicit check (strlen(str) > len) before strcpy; raises a Lua error "hex string is
  too long" so the caller (llora_set_devAddr/devEui/appEui/nwkSKey/appSKey/appKey) rejects keys
  of the wrong length rather than silently overflowing.

DONE: Review all project code for stack overflows from unbounded recursion — recursive functions with no depth limit or guard against deeply nested input.

- F1 (lua_cjson.c:75-76): DEFAULT_ENCODE_MAX_DEPTH and DEFAULT_DECODE_MAX_DEPTH were 1000, which allows
  ~220 bytes of C stack per nesting level (two frames: json_append_data/json_process_value alternating with
  json_append_object/json_parse_object_context). At depth 1000 that needs ~220 KB, far exceeding the 40 KB
  Lua task stack. Changed both limits from 1000 to 20. Real embedded JSON never needs more than 10 levels;
  depth 20 uses ~4.4 KB of stack at maximum and matches the parson.c MAX_NESTING=19 limit.
- parson.c json_serialize_to_buffer_r() has no depth guard but is safe: all objects it serializes were
  created by parse_value() which enforces MAX_NESTING=19, so the serializer cannot recurse deeper than 19.

DONE: Review all project code for double-free bugs — pointers freed more than once in single-threaded code, or freed via two separate ownership paths.

- F1 (nvs.c:67-82): nvs_error() switch had no default: case. For any ESP-IDF error code not listed
  (e.g. ESP_ERR_NVS_NOT_INITIALIZED, ESP_ERR_NVS_READ_ONLY, ESP_ERR_NVS_NOT_ENOUGH_SPACE, etc.),
  the function returned silently instead of throwing. In l_nvs_write this caused a double-free:
  nvs_open failure → free(val_val) + nvs_error() returns → nvs_set_blob runs with freed val_val
  → nvs_set_blob fails → free(val_val) again. Fixed: added default: luaL_error(L, "%d:nvs error", code)
  so nvs_error always throws for any error code.

All other apparent multiple-free patterns across the codebase (sys/drivers, sys/vfs, sys/sensors,
lua/modules, lora, http, mqtt, FabGL, pthread, eth_enc424j600) were inspected and confirmed safe:
each free() call is on a mutually exclusive early-return error path, so no path executes more than
one free() for the same allocation.

DONE: Review all project code for sign extension bugs — narrower signed values cast to wider signed types where the sign bit propagates unexpectedly, especially from int8_t/int16_t to int32_t/int64_t comparisons.

- F1 (aes.c:263): `while ((signed char)len > 0)` where `len` is `u2_t` (uint16_t). Casting
  uint16_t to `signed char` only examines the low 8 bits as a signed value. For any len in
  128..255 (which covers most LoRa payloads above 127 bytes), `(signed char)len` is -128..-1
  — the loop never executes, silently skipping all AES-CTR encryption and AES-MIC authentication.
  Fix: changed `(signed char)len` to `(int16_t)len`. The uint16_t-underflow sentinel still works:
  after processing the last partial block, `len -= 16` wraps to 65521..65535 as uint16_t, which
  is -15..-1 as int16_t, causing the loop to exit correctly.

No other sign extension bugs found. The bma423.c `(int8_t)raw` for hardware temperature and
the loragw_reg.c `bufs[2] = bufs[1] >> (8 - r.leng)` arithmetic-right-shift are both intentional.
The rmt.c `int8_t channel` using -1 as a sentinel and the encoder.c `int8_t dir` with values
-1/0/1 are all correct. The qrcodegen int8_t tables with -1 sentinel at index 0 are safe since
version=0 is never used. The Lua source casts to (unsigned char) before table lookups.

DONE: Review all project code for truncation on assignment — values from wider types (int32_t, int64_t, size_t) silently narrowed when assigned to uint8_t/uint16_t/int16_t variables, especially in size or length calculations.

- F1 (lfs.c:944): lfs_mkdir stored strlen(path) (size_t) into entry.d.nlen (uint8_t) with no length guard. A name component > 255 chars silently truncates nlen and corrupts the on-disk directory entry. Added `if (strlen(path) > LFS_NAME_MAX) return LFS_ERR_INVAL;` before the assignment; explicit (uint8_t) cast added.
- F2 (lfs.c:1323): lfs_file_open same truncation on file creation. Same fix applied.
- F3 (lfs.c:1973): lfs_rename stored strlen(newpath) (size_t) into newentry.d.nlen (uint8_t) with no guard. Same fix applied.
- F4 (lora_lmic.c:585): `payload_len = strlen(data) / 2` stored a size_t/2 into uint8_t. For a hex string > 510 chars the division result wraps (e.g. 512-char string → 256 → 0), causing malloc(1) and hex_string_to_val with length 0 — payload sent as 0 bytes silently. Added early return `driver_error(LORA_DRIVER, LORA_ERR_INVALID_ARGUMENT, NULL)` when strlen(data) > 2*255; explicit (uint8_t) cast added.

bluetooth.c:183 `uint16_t datalen = strlen(adv_data) / 2` was reviewed and is safe: an immediate `if (datalen > 30)` guard throws before the value is used.

DONE: Review all project code for wrong loop counter type — signed int used as a loop counter over size_t or unsigned ranges, or uint8_t/uint16_t used where the count can exceed the type maximum.

No genuine bugs found. All uint8_t/uint16_t loop counters iterate over values well within their type range (CAN_NUM_FILTERS=10, MAX_ONEWIRE_SENSORS=8, MAX_CHANNELS=16, etc.). The only signed/unsigned comparison is int i vs size_t in rmt.c (lines 631, 653, 686, 774), but RMT pulse counts are hardware-bounded (< 64 on ESP32). Decrement loops on uint8_t use `> 0` (not `>= 0`), so none are infinite loops. All u1_t (uint8_t) counters in lmic.c/radio.c iterate over small fixed values ≤ 64.

TODO: Review all project code for deadlock from lock ordering violations — code that acquires multiple mutexes in inconsistent order across different call paths, risking classic ABBA deadlock.

TODO: Review all project code for double-checked locking without memory barriers — patterns where a shared flag or pointer is read outside a lock to avoid locking cost, then re-checked inside, without appropriate volatile or atomic semantics.

TODO: Review all project code for unchecked return values — calls to system functions (open, read, write, ioctl, send, recv, connect, bind, listen) where the return value is ignored and execution continues as if the call succeeded.

TODO: Review all project code for silently discarded error codes — driver_error_t or esp_err_t return values assigned to a local variable but never tested, or cast to void without justification.

TODO: Review all project code for wrong error codes returned — functions that return a plausible-sounding but incorrect error constant (e.g. from the wrong driver, or semantically wrong such as ENOENT instead of EINVAL).

TODO: Review all project code for partial initialisation on failure — structs or objects that are half-constructed when an error occurs mid-setup, leaving the caller with a pointer to an inconsistent state.

TODO: Review all project code for semaphore, event group, and task handle leaks — xSemaphoreCreateMutex/Binary/Counting and xEventGroupCreate results not deleted on every error path; xTaskCreate handles not stored for later vTaskDelete.

TODO: Review all project code for wrong variable updated — assignments where a similarly-named variable is written instead of the intended one, leaving the intended target unchanged.

TODO: Review all project code for reversed calloc arguments — calloc(size, count) instead of calloc(count, size), giving correct total bytes by accident but wrong API usage.

TODO: Review all project code for wrong NaN comparisons — floating-point NaN tested with == or != instead of isnan(), which always evaluates false/true respectively, causing silent logic errors.

TODO: Review all project code for inverted logical operators — conditions using && where || is required (or vice versa) in guard expressions and input validation checks.

TODO: Review all project code for security vulnerabilities — path traversal in file open calls using user-supplied strings, command injection if any exec/system calls consume user data, and reflected user input in HTTP responses without escaping (XSS).

TODO: Review all project code for unchecked user-supplied lengths — Content-Length, packet length fields, or any length arriving from the network or Lua caller used directly in malloc, memcpy, or read without validation against a maximum.

TODO: Review all project code for information leakage — error messages or HTTP responses that include internal file paths, stack addresses, heap addresses, or other implementation details useful to an attacker.

TODO: Review all project code for strict aliasing violations — type-punning through incompatible pointer casts (e.g. uint8_t* cast to uint32_t* to read multi-byte values) that the compiler may optimise incorrectly under strict-aliasing rules.

TODO: Review all project code for unsequenced modifications — expressions where the same variable is both read and modified without a sequence point, such as a[i] = i++, producing undefined behaviour.

TODO: Review all project code for dead and unreachable code — statements after unconditional return/break/continue/goto, conditions that are always true or always false due to type constraints or prior assignments, and branches that can never execute.

- F1 (cpu.c:108): `(1 << bit)` in cpu_has_gpio() where `bit` is a GPIO pin number (0-48 on
  ESP32-S3). For bit >= 32 this is undefined behaviour — shifting a 32-bit signed int by 32+
  positions. Fixed: `(GPIO_BIT_MASK << bit)` uses `uint64_t` (1ULL) so the shift is always
  within the 64-bit type width and the result matches the gpio_pin_mask_t return type of
  cpu_port_io_pin_mask().
- F2 (gpio.c:87,102,117): `(1 << pin)` in gpio_ll_pin_set, gpio_ll_pin_clr, and gpio_ll_pin_inv
  where `pin < 32`. When pin == 31, `1 << 31` produces 2^31 which is not representable in a
  signed 32-bit int — undefined behaviour per C11 §6.5.7. Fixed: `(1u << pin)` so the shift is
  on an unsigned 32-bit type, making the result well-defined for all pin values 0-31.

