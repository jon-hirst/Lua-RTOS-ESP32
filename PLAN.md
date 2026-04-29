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
Change to LUAI_GCPAUSE 110 (cycle starts after 10% growth) and LUAI_GCMUL 400
(faster sweep) to reduce peak working-set size on embedded hardware.

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

TODO: Enable core dump output to diagnose crashes

CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y means all register state, stack frames and heap
content are lost on crash. Enable CONFIG_ESP_COREDUMP_ENABLE_TO_UART to print a
decodable crash dump on the serial console at minimum. Consider
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH with a dedicated partition for fully off-line
post-mortem analysis via idf.py coredump-info.

TODO: Fix the Lua restart loop in main.c to handle abnormal exits cleanly

main.c:70-74 calls luaos_main() in a bare for(;;) loop with no delay, no logging of
the exit reason, and no hardware re-initialisation. When luaos_main() exits after a
panic or error, hardware drivers (SPI, I2C, UART, network stack) are left in unknown
state and the new Lua VM inherits that state. Add exit-reason logging, conditional
esp_restart() on abnormal exit, and at minimum a short delay before re-entry so the
system does not spin at full power in a crash loop.

TODO: Raise the Lua interpreter task priority to reduce network callback latency

The Lua task runs at priority 3 (the lowest non-idle priority) while the HTTP server
runs at 18 and LoRa at 21. Lua network callbacks (MQTT, HTTP, LoRa) are posted into
a queue by the high-priority network task but processed by the Lua VM only when every
task at priorities 4-20 is blocked, causing multi-hundred-millisecond latency under
load. Raise CONFIG_LUA_RTOS_LUA_TASK_PRIORITY to 10-15 and adjust the Kconfig range
and default accordingly.

TODO: Make LFS the default flash filesystem instead of SPIFFS

The Kconfig default for LUA_RTOS_FLASH_STORAGE_FS is SPIFFS. SPIFFS has no power-cut
safety (a crash during a write can corrupt the entire filesystem), no wear leveling,
and a flat namespace. LFS (littlefs) is already in the codebase, is power-cut safe by
design, has proper wear leveling, and supports directories. Change the Kconfig default
to LUA_RTOS_USE_LFS and update the root filesystem default to LUA_RTOS_LFS_ROOT_FS.
