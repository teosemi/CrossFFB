#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>

#define LOGITECH_VID 0x046D
#define G29_PID      0xC24F

#define LISTEN_HOST "127.0.0.1"
#define DEFAULT_LISTEN_PORT 54321
#define DEFAULT_CONTROL_PORT 54322

#define REPORT_ID   0x00
#define OUTPUT_SIZE 16

// Runtime defaults. The menu bar app will pass these as CLI args.
#define DEFAULT_WHEEL_RANGE_DEGREES 900

#define DI_FORCE_MAX 10000.0

// Step 20B: forza aumentata.
// Step 20 precedente era 0.35 ed era troppo debole.
#define DEFAULT_FORCE_GAIN 1.00

// Direzione confermata corretta dal test Step 20.
#define DEFAULT_INVERT_FORCE 0

// Evita spam di STOP F3 quando ETS2 invia molti STOP consecutivi.
#define STOP_THROTTLE_SECONDS 0.100

static volatile sig_atomic_t g_running = 1;

static IOHIDManagerRef g_hid_manager = NULL;
static IOHIDDeviceRef g_wheel = NULL;

static int g_client_fd = -1;
static int g_server_fd = -1;
static int g_control_server_fd = -1;
static int g_control_client_fd = -1;

static int g_last_force_byte = -1;
static int g_hid_force_count = 0;
static int g_hid_stop_count = 0;
static int g_hid_stop_skipped_count = 0;
static int g_message_count = 0;

static double g_force_gain = DEFAULT_FORCE_GAIN;
static int g_invert_force = DEFAULT_INVERT_FORCE;
static int g_wheel_range_degrees = DEFAULT_WHEEL_RANGE_DEGREES;
static int g_listen_port = DEFAULT_LISTEN_PORT;
static int g_control_port = DEFAULT_CONTROL_PORT;

static pid_t g_parent_pid = -1;
static double g_last_parent_check_ts = 0.0;

static double g_last_stop_ts = 0.0;

static void poll_control_socket(void);
static void check_parent_watchdog(void);

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static void timestamp(char *buffer, size_t size)
{
    time_t t = time(NULL);
    struct tm tm_value;
    localtime_r(&t, &tm_value);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &tm_value);
}

static void log_line(const char *fmt, ...)
{
    char ts[64];
    timestamp(ts, sizeof(ts));

    printf("[%s] ", ts);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
    fflush(stdout);
}

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;

    if (g_client_fd >= 0)
    {
        shutdown(g_client_fd, SHUT_RDWR);
        close(g_client_fd);
        g_client_fd = -1;
    }

    if (g_server_fd >= 0)
    {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }

    if (g_control_client_fd >= 0)
    {
        shutdown(g_control_client_fd, SHUT_RDWR);
        close(g_control_client_fd);
        g_control_client_fd = -1;
    }

    if (g_control_server_fd >= 0)
    {
        shutdown(g_control_server_fd, SHUT_RDWR);
        close(g_control_server_fd);
        g_control_server_fd = -1;
    }
}

static void check_parent_watchdog(void)
{
    if (g_parent_pid <= 1)
    {
        return;
    }

    double t = now_seconds();

    if ((t - g_last_parent_check_ts) < 0.500)
    {
        return;
    }

    g_last_parent_check_ts = t;

    if (kill(g_parent_pid, 0) == 0)
    {
        return;
    }

    if (errno == ESRCH)
    {
        log_line("parent process pid=%d disappeared, shutting down bridge", (int)g_parent_pid);
        handle_signal(SIGTERM);
    }
}


static int get_int_property(IOHIDDeviceRef dev, CFStringRef key, int default_value)
{
    CFTypeRef value = IOHIDDeviceGetProperty(dev, key);

    if (!value || CFGetTypeID(value) != CFNumberGetTypeID())
    {
        return default_value;
    }

    int result = default_value;
    CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &result);
    return result;
}

static CFMutableDictionaryRef create_matching_dictionary(int vid, int pid)
{
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );

    CFNumberRef vidNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef pidNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);

    CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), vidNumber);
    CFDictionarySetValue(dict, CFSTR(kIOHIDProductIDKey), pidNumber);

    CFRelease(vidNumber);
    CFRelease(pidNumber);

    return dict;
}

static IOHIDDeviceRef find_main_joystick_device(CFSetRef devices)
{
    CFIndex count = CFSetGetCount(devices);
    IOHIDDeviceRef *deviceArray = calloc((size_t)count, sizeof(IOHIDDeviceRef));

    if (!deviceArray)
    {
        return NULL;
    }

    CFSetGetValues(devices, (const void **)deviceArray);

    IOHIDDeviceRef result = NULL;

    for (CFIndex i = 0; i < count; i++)
    {
        IOHIDDeviceRef dev = deviceArray[i];

        int usagePage = get_int_property(dev, CFSTR(kIOHIDPrimaryUsagePageKey), -1);
        int usage = get_int_property(dev, CFSTR(kIOHIDPrimaryUsageKey), -1);
        int outputSize = get_int_property(dev, CFSTR(kIOHIDMaxOutputReportSizeKey), -1);

        log_line(
            "HID candidate #%ld usage_page=0x%04X usage=0x%04X outputSize=%d",
            (long)i + 1,
            usagePage,
            usage,
            outputSize
        );

        if (usagePage == 0x0001 && usage == 0x0004)
        {
            result = dev;
            break;
        }
    }

    free(deviceArray);
    return result;
}

static void print_cmd7(const uint8_t cmd7[7])
{
    for (int i = 0; i < 7; i++)
    {
        printf(" %02X", cmd7[i]);
    }
}

static IOReturn send_command7(const uint8_t cmd7[7], const char *label, int verbose)
{
    if (!g_wheel)
    {
        log_line("HID %s skipped: wheel not open", label);
        return kIOReturnNotOpen;
    }

    uint8_t buffer[OUTPUT_SIZE];
    memset(buffer, 0, sizeof(buffer));

    for (int i = 0; i < 7; i++)
    {
        buffer[i] = cmd7[i];
    }

    IOReturn rc = IOHIDDeviceSetReport(
        g_wheel,
        kIOHIDReportTypeOutput,
        REPORT_ID,
        buffer,
        OUTPUT_SIZE
    );

    if (verbose || rc != kIOReturnSuccess)
    {
        char ts[64];
        timestamp(ts, sizeof(ts));

        printf("[%s] HID %s cmd7:", ts, label);
        print_cmd7(cmd7);
        printf(" rc=0x%08X\n", rc);
        fflush(stdout);
    }

    return rc;
}

static void wheel_stop_all(const char *reason, int verbose, int force_send)
{
    double t = now_seconds();

    if (!force_send && (t - g_last_stop_ts) < STOP_THROTTLE_SECONDS)
    {
        g_hid_stop_skipped_count++;

        if (verbose && (g_hid_stop_skipped_count % 50) == 0)
        {
            log_line(
                "HID STOP throttled reason=%s skipped_total=%d",
                reason,
                g_hid_stop_skipped_count
            );
        }

        return;
    }

    g_last_stop_ts = t;

    const uint8_t stopAll[7] = {0xF3, 0, 0, 0, 0, 0, 0};

    IOReturn rc = send_command7(stopAll, reason, verbose);

    if (rc == kIOReturnSuccess)
    {
        g_hid_stop_count++;
    }

    g_last_force_byte = -1;
}

static void wheel_spring_off(int verbose)
{
    const uint8_t springOff[7] = {0xF5, 0, 0, 0, 0, 0, 0};
    send_command7(springOff, "springOff F5", verbose);
}

static void wheel_set_range(int degrees, int verbose)
{
    int range = degrees;

    if (range < 40)
    {
        range = 40;
    }

    if (range > 900)
    {
        range = 900;
    }

    uint8_t low = (uint8_t)(range & 0x00FF);
    uint8_t high = (uint8_t)((range & 0xFF00) >> 8);

    // Swift app equivalent:
    // setRangeCommand(range) => F8 81 LOW HIGH 00 00 00
    uint8_t setRange[7] = {0xF8, 0x81, low, high, 0x00, 0x00, 0x00};

    char label[64];
    snprintf(label, sizeof(label), "setRange %d deg F8", range);

    send_command7(setRange, label, verbose);
}

static void wheel_fixed_loop_on(int verbose)
{
    const uint8_t fixedLoopOn[7] = {0x0D, 0x01, 0, 0, 0, 0, 0};
    send_command7(fixedLoopOn, "fixedLoopOn 0D", verbose);
}

static void wheel_init(void)
{
    log_line("HID init sequence begin");

    wheel_stop_all("init stopAll F3", 1, 1);
    usleep(100000);

    wheel_spring_off(1);
    usleep(100000);

    wheel_set_range(g_wheel_range_degrees, 1);
    usleep(100000);

    wheel_fixed_loop_on(1);
    usleep(100000);

    log_line("HID init sequence end");
}

static int clamp_int(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int magnitude_to_force_byte(int magnitude)
{
    double normalized = (double)magnitude / DI_FORCE_MAX;

    if (g_invert_force)
    {
        normalized = -normalized;
    }

    normalized *= g_force_gain;

    if (normalized < -1.0) normalized = -1.0;
    if (normalized > 1.0) normalized = 1.0;

    int delta = (int)(normalized * 127.0);
    int forceByte = 0x80 + delta;

    return clamp_int(forceByte, 0x00, 0xFF);
}

static void make_app_constant_command(uint8_t out[7], int forceByte)
{
    // Comando confermato funzionante:
    // 11 00 XX 80 80 80 00
    out[0] = 0x11;
    out[1] = 0x00;
    out[2] = (uint8_t)forceByte;
    out[3] = 0x80;
    out[4] = 0x80;
    out[5] = 0x80;
    out[6] = 0x00;
}

static void wheel_send_constant_from_magnitude(int magnitude)
{
    int forceByte = magnitude_to_force_byte(magnitude);

    if (forceByte == g_last_force_byte)
    {
        return;
    }

    uint8_t cmd[7];
    make_app_constant_command(cmd, forceByte);

    IOReturn rc = send_command7(cmd, "constant 11", 0);

    if (rc == kIOReturnSuccess)
    {
        g_hid_force_count++;
        g_last_force_byte = forceByte;
    }

    static double lastLog = 0.0;
    double t = now_seconds();

    if (t - lastLog >= 0.25 || rc != kIOReturnSuccess)
    {
        double normalized = ((double)magnitude / DI_FORCE_MAX) * g_force_gain;

        if (g_invert_force)
        {
            normalized = -normalized;
        }

        if (normalized < -1.0) normalized = -1.0;
        if (normalized > 1.0) normalized = 1.0;

        log_line(
            "FORCE magnitude=%+6d normalized_gain=%+.4f forceByte=0x%02X hid_force_count=%d",
            magnitude,
            normalized,
            forceByte,
            g_hid_force_count
        );

        lastLog = t;
    }
}

static int open_wheel(void)
{
    g_hid_manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

    if (!g_hid_manager)
    {
        log_line("IOHIDManagerCreate failed");
        return 0;
    }

    CFMutableDictionaryRef match = create_matching_dictionary(LOGITECH_VID, G29_PID);
    IOHIDManagerSetDeviceMatching(g_hid_manager, match);
    CFRelease(match);

    IOReturn managerRc = IOHIDManagerOpen(g_hid_manager, kIOHIDOptionsTypeNone);
    log_line("IOHIDManagerOpen rc=0x%08X", managerRc);

    if (managerRc != kIOReturnSuccess)
    {
        return 0;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(g_hid_manager);

    if (!devices)
    {
        log_line("No G29 HID devices found");
        return 0;
    }

    g_wheel = find_main_joystick_device(devices);

    if (!g_wheel)
    {
        log_line("Main joystick HID device not found");
        CFRelease(devices);
        return 0;
    }

    IOReturn openRc = IOHIDDeviceOpen(g_wheel, kIOHIDOptionsTypeNone);
    log_line("IOHIDDeviceOpen main joystick rc=0x%08X", openRc);

    CFRelease(devices);

    if (openRc != kIOReturnSuccess)
    {
        g_wheel = NULL;
        return 0;
    }

    wheel_init();
    return 1;
}

static void close_wheel(void)
{
    if (g_wheel)
    {
        log_line("HID final stop");
        wheel_stop_all("final stopAll F3", 1, 1);
        usleep(100000);
        wheel_spring_off(1);

        IOHIDDeviceClose(g_wheel, kIOHIDOptionsTypeNone);
        g_wheel = NULL;
        log_line("HID wheel closed");
    }

    if (g_hid_manager)
    {
        IOHIDManagerClose(g_hid_manager, kIOHIDOptionsTypeNone);
        CFRelease(g_hid_manager);
        g_hid_manager = NULL;
    }
}

static int create_server_socket_on_port(int port, const char *name)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        log_line("%s socket failed: %s", name, strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, LISTEN_HOST, &addr.sin_addr);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        log_line("%s bind %s:%d failed: %s", name, LISTEN_HOST, port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 8) < 0)
    {
        log_line("%s listen failed: %s", name, strerror(errno));
        close(fd);
        return -1;
    }

    log_line("%s listening on %s:%d", name, LISTEN_HOST, port);
    return fd;
}

static void handle_line(const char *line)
{
    g_message_count++;

    if (strncmp(line, "SET_CONSTANT magnitude=", 23) == 0)
    {
        int magnitude = atoi(line + 23);
        wheel_send_constant_from_magnitude(magnitude);
        return;
    }

    if (strcmp(line, "STOP") == 0)
    {
        wheel_stop_all("rx STOP F3", 0, 0);
        return;
    }

    if (strcmp(line, "UNACQUIRE") == 0)
    {
        log_line("RX UNACQUIRE -> stopAll");
        wheel_stop_all("rx UNACQUIRE F3", 1, 0);
        return;
    }

    if (strcmp(line, "ACQUIRE") == 0)
    {
        log_line("RX ACQUIRE");
        return;
    }

    if (strncmp(line, "HELLO", 5) == 0)
    {
        log_line("RX %s", line);
        return;
    }

    if (strncmp(line, "CREATE_EFFECT", 13) == 0)
    {
        log_line("RX %s", line);
        return;
    }

    if (strncmp(line, "FF_COMMAND", 10) == 0)
    {
        log_line("RX %s -> stopAll safety", line);
        wheel_stop_all("rx FF_COMMAND F3", 1, 0);
        return;
    }

    log_line("RX unhandled: %s", line);
}

static void send_control_reply(int client_fd, const char *text)
{
    if (client_fd < 0 || !text)
    {
        return;
    }

    send(client_fd, text, strlen(text), 0);
    send(client_fd, "\n", 1, 0);
}

static void handle_control_line(int client_fd, const char *line)
{
    if (strncmp(line, "SET_GAIN ", 9) == 0)
    {
        double new_gain = strtod(line + 9, NULL);

        if (new_gain < 0.0)
        {
            new_gain = 0.0;
        }

        if (new_gain > 2.0)
        {
            new_gain = 2.0;
        }

        g_force_gain = new_gain;

        char reply[128];
        snprintf(reply, sizeof(reply), "OK SET_GAIN %.2f", g_force_gain);
        send_control_reply(client_fd, reply);

        log_line("CONTROL SET_GAIN %.2f", g_force_gain);
        return;
    }

    if (strncmp(line, "SET_RANGE ", 10) == 0)
    {
        int new_range = atoi(line + 10);

        if (new_range < 40)
        {
            new_range = 40;
        }

        if (new_range > 900)
        {
            new_range = 900;
        }

        g_wheel_range_degrees = new_range;
        wheel_set_range(g_wheel_range_degrees, 1);

        char reply[128];
        snprintf(reply, sizeof(reply), "OK SET_RANGE %d", g_wheel_range_degrees);
        send_control_reply(client_fd, reply);

        log_line("CONTROL SET_RANGE %d", g_wheel_range_degrees);
        return;
    }

    if (strcmp(line, "RESET_WHEEL") == 0)
    {
        log_line("CONTROL RESET_WHEEL");

        wheel_stop_all("control reset stopAll F3", 1, 1);
        usleep(100000);

        wheel_spring_off(1);
        usleep(100000);

        wheel_set_range(g_wheel_range_degrees, 1);
        usleep(100000);

        wheel_fixed_loop_on(1);

        send_control_reply(client_fd, "OK RESET_WHEEL");
        return;
    }

    if (strcmp(line, "GET_STATUS") == 0)
    {
        char reply[256];

        snprintf(
            reply,
            sizeof(reply),
            "OK STATUS gain=%.2f range=%d invert=%d force_count=%d stop_count=%d skipped_stop=%d messages=%d",
            g_force_gain,
            g_wheel_range_degrees,
            g_invert_force,
            g_hid_force_count,
            g_hid_stop_count,
            g_hid_stop_skipped_count,
            g_message_count
        );

        send_control_reply(client_fd, reply);
        return;
    }

    send_control_reply(client_fd, "ERR UNKNOWN_COMMAND");
    log_line("CONTROL unknown command: %s", line);
}

static void handle_control_client(int client_fd)
{
    char recvbuf[1024];
    char linebuf[2048];
    size_t line_len = 0;

    log_line("CONTROL client connected");

    while (g_running)
    {
        poll_control_socket();
        check_parent_watchdog();

        ssize_t n = recv(client_fd, recvbuf, sizeof(recvbuf), MSG_DONTWAIT);

        if (n == 0)
        {
            break;
        }

        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                usleep(10000);
                continue;
            }

            if (errno == ECONNRESET)
            {
                break;
            }

            log_line("CONTROL recv failed: %s", strerror(errno));
            break;
        }

        for (ssize_t i = 0; i < n; i++)
        {
            char c = recvbuf[i];

            if (c == '\n')
            {
                linebuf[line_len] = '\0';

                if (line_len > 0 && linebuf[line_len - 1] == '\r')
                {
                    linebuf[line_len - 1] = '\0';
                }

                if (linebuf[0] != '\0')
                {
                    handle_control_line(client_fd, linebuf);
                }

                line_len = 0;
            }
            else
            {
                if (line_len + 1 < sizeof(linebuf))
                {
                    linebuf[line_len++] = c;
                }
                else
                {
                    line_len = 0;
                    send_control_reply(client_fd, "ERR LINE_TOO_LONG");
                }
            }
        }
    }

    log_line("CONTROL client disconnected");
}

static void poll_control_socket(void)
{
    if (g_control_server_fd < 0)
    {
        return;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(g_control_server_fd, &readfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int rc = select(g_control_server_fd + 1, &readfds, NULL, NULL, &tv);

    if (rc <= 0)
    {
        return;
    }

    if (!FD_ISSET(g_control_server_fd, &readfds))
    {
        return;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    g_control_client_fd = accept(g_control_server_fd, (struct sockaddr *)&client_addr, &client_len);

    if (g_control_client_fd < 0)
    {
        if (errno != EINTR)
        {
            log_line("CONTROL accept failed: %s", strerror(errno));
        }

        return;
    }

    handle_control_client(g_control_client_fd);

    if (g_control_client_fd >= 0)
    {
        close(g_control_client_fd);
        g_control_client_fd = -1;
    }
}

static void handle_client(int client_fd)
{
    char recvbuf[4096];
    char linebuf[8192];
    size_t line_len = 0;

    log_line("TCP client connected");

    while (g_running)
    {
        poll_control_socket();
        check_parent_watchdog();

        ssize_t n = recv(client_fd, recvbuf, sizeof(recvbuf), MSG_DONTWAIT);

        if (n == 0)
        {
            log_line("TCP client disconnected");
            break;
        }

        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                usleep(10000);
                continue;
            }

            log_line("recv failed: %s", strerror(errno));
            break;
        }

        for (ssize_t i = 0; i < n; i++)
        {
            char c = recvbuf[i];

            if (c == '\n')
            {
                linebuf[line_len] = '\0';

                if (line_len > 0 && linebuf[line_len - 1] == '\r')
                {
                    linebuf[line_len - 1] = '\0';
                }

                handle_line(linebuf);
                line_len = 0;
            }
            else
            {
                if (line_len + 1 < sizeof(linebuf))
                {
                    linebuf[line_len++] = c;
                }
                else
                {
                    line_len = 0;
                    log_line("line buffer overflow, dropped");
                }
            }
        }
    }

    wheel_stop_all("client disconnect stopAll F3", 1, 1);
}

static void print_usage(const char *program_name)
{
    printf("Usage: %s [options]\n", program_name);
    printf("\n");
    printf("Options:\n");
    printf("  --gain <value>      Force gain, default %.2f, example 1.00\n", DEFAULT_FORCE_GAIN);
    printf("  --range <degrees>   Steering range 40..900, default %d\n", DEFAULT_WHEEL_RANGE_DEGREES);
    printf("  --invert <0|1>      Invert force direction, default %d\n", DEFAULT_INVERT_FORCE);
    printf("  --port <port>       Game TCP listen port, default %d\n", DEFAULT_LISTEN_PORT);
    printf("  --control-port <p>  App control TCP port, default %d\n", DEFAULT_CONTROL_PORT);
    printf("  --help              Show this help\n");
    printf("\n");
}

static int parse_int_arg(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);

    if (!text || *text == '\0' || !end || *end != '\0')
    {
        return 0;
    }

    *out = (int)value;
    return 1;
}

static int parse_double_arg(const char *text, double *out)
{
    char *end = NULL;
    double value = strtod(text, &end);

    if (!text || *text == '\0' || !end || *end != '\0')
    {
        return 0;
    }

    *out = value;
    return 1;
}

static int parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }

        if (strcmp(argv[i], "--gain") == 0)
        {
            if (i + 1 >= argc || !parse_double_arg(argv[i + 1], &g_force_gain))
            {
                fprintf(stderr, "Invalid --gain value\n");
                return -1;
            }

            i++;
            continue;
        }

        if (strcmp(argv[i], "--range") == 0)
        {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &g_wheel_range_degrees))
            {
                fprintf(stderr, "Invalid --range value\n");
                return -1;
            }

            i++;
            continue;
        }

        if (strcmp(argv[i], "--invert") == 0)
        {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &g_invert_force))
            {
                fprintf(stderr, "Invalid --invert value\n");
                return -1;
            }

            i++;
            continue;
        }

        if (strcmp(argv[i], "--port") == 0)
        {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &g_listen_port))
            {
                fprintf(stderr, "Invalid --port value\n");
                return -1;
            }

            i++;
            continue;
        }

        if (strcmp(argv[i], "--control-port") == 0)
        {
            if (i + 1 >= argc || !parse_int_arg(argv[i + 1], &g_control_port))
            {
                fprintf(stderr, "Invalid --control-port value\n");
                return -1;
            }

            i++;
            continue;
        }

        fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        print_usage(argv[0]);
        return -1;
    }

    if (g_force_gain < 0.0)
    {
        g_force_gain = 0.0;
    }

    if (g_force_gain > 2.0)
    {
        g_force_gain = 2.0;
    }

    if (g_wheel_range_degrees < 40)
    {
        g_wheel_range_degrees = 40;
    }

    if (g_wheel_range_degrees > 900)
    {
        g_wheel_range_degrees = 900;
    }

    g_invert_force = g_invert_force ? 1 : 0;

    if (g_listen_port < 1 || g_listen_port > 65535)
    {
        fprintf(stderr, "Invalid --port value, must be 1..65535\n");
        return -1;
    }

    if (g_control_port < 1 || g_control_port > 65535)
    {
        fprintf(stderr, "Invalid --control-port value, must be 1..65535\n");
        return -1;
    }

    if (g_control_port == g_listen_port)
    {
        fprintf(stderr, "--control-port must be different from --port\n");
        return -1;
    }

    return 1;
}

int main(int argc, char **argv)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int parse_result = parse_args(argc, argv);

    if (parse_result <= 0)
    {
        return parse_result == 0 ? 0 : 1;
    }

    g_parent_pid = getppid();

    log_line("g29_ffb_bridge Step26 parent watchdog starting");
    log_line("gain=%.2f invert=%d range=%d port=%d control_port=%d parent_pid=%d STOP_THROTTLE_SECONDS=%.3f",
             g_force_gain,
             g_invert_force,
             g_wheel_range_degrees,
             g_listen_port,
             g_control_port,
             (int)g_parent_pid,
             STOP_THROTTLE_SECONDS);

    if (!open_wheel())
    {
        log_line("Failed to open wheel");
        close_wheel();
        return 1;
    }

    g_server_fd = create_server_socket_on_port(g_listen_port, "GAME TCP");

    if (g_server_fd < 0)
    {
        close_wheel();
        return 1;
    }

    g_control_server_fd = create_server_socket_on_port(g_control_port, "CONTROL TCP");

    if (g_control_server_fd < 0)
    {
        if (g_server_fd >= 0)
        {
            close(g_server_fd);
            g_server_fd = -1;
        }

        close_wheel();
        return 1;
    }

    while (g_running)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        poll_control_socket();
        check_parent_watchdog();

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_server_fd, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int select_rc = select(g_server_fd + 1, &readfds, NULL, NULL, &tv);

        if (select_rc < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            log_line("GAME select failed: %s", strerror(errno));
            break;
        }

        if (select_rc == 0)
        {
            continue;
        }

        g_client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (g_client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            log_line("accept failed: %s", strerror(errno));
            break;
        }

        handle_client(g_client_fd);

        close(g_client_fd);
        g_client_fd = -1;
    }

    if (g_client_fd >= 0)
    {
        close(g_client_fd);
        g_client_fd = -1;
    }

    if (g_server_fd >= 0)
    {
        close(g_server_fd);
        g_server_fd = -1;
    }

    if (g_control_client_fd >= 0)
    {
        close(g_control_client_fd);
        g_control_client_fd = -1;
    }

    if (g_control_server_fd >= 0)
    {
        close(g_control_server_fd);
        g_control_server_fd = -1;
    }

    close_wheel();

    log_line(
        "g29_ffb_bridge stopped messages=%d hid_force_count=%d hid_stop_count=%d hid_stop_skipped=%d",
        g_message_count,
        g_hid_force_count,
        g_hid_stop_count,
        g_hid_stop_skipped_count
    );

    return 0;
}
