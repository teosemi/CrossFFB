#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unknwn.h>
#include <initguid.h>
#include <dinput.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

static HMODULE g_realDInput8 = NULL;

typedef HRESULT (WINAPI *DirectInput8Create_t)(
    HINSTANCE hinst,
    DWORD dwVersion,
    REFIID riidltf,
    LPVOID* ppvOut,
    LPUNKNOWN punkOuter
);

static DirectInput8Create_t g_realDirectInput8Create = NULL;

// Builds the path of a file sitting next to the running game executable.
static void build_sibling_path(const char* fileName, char* out)
{
    char exePath[MAX_PATH] = {0};

    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);

    if (len > 0)
    {
        lstrcpyA(out, exePath);

        char* lastSlash = NULL;
        for (char* p = out; *p; ++p)
        {
            if (*p == '\\' || *p == '/')
            {
                lastSlash = p;
            }
        }

        if (lastSlash)
        {
            *(lastSlash + 1) = '\0';
            lstrcatA(out, fileName);
            return;
        }
    }

    lstrcpyA(out, fileName);
}

static void log_write(const char* fmt, va_list args)
{
    char logPath[MAX_PATH] = {0};

    build_sibling_path("dinput8_proxy.log", logPath);

    FILE* f = fopen(logPath, "ab");
    if (!f)
    {
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    fprintf(
        f,
        "[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond
    );

    vfprintf(f, fmt, args);

    fprintf(f, "\r\n");
    fclose(f);
}

static void log_line(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_write(fmt, args);
    va_end(args);
}

#define VERBOSE_LOG_MARKER_NAME "crossffb_verbose_log.enabled"

// Per-call tracing used to be written unconditionally, one fopen/fclose per
// line, from the thread delivering force feedback. A single session left
// gigabytes behind, so it is now opt-in, through either CROSSFFB_PROXY_LOG=1
// in the bottle environment or a marker file next to the game executable,
// which is what the CrossFFB Setup toggle writes. The choice is read once and
// cached, so it applies from the next time the game starts.
static bool verbose_logging_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
    {
        char value[16] = {0};
        DWORD len = GetEnvironmentVariableA("CROSSFFB_PROXY_LOG", value, sizeof(value));

        enabled = (len > 0 && len < sizeof(value) && value[0] != '0') ? 1 : 0;

        if (enabled == 0)
        {
            char markerPath[MAX_PATH] = {0};

            build_sibling_path(VERBOSE_LOG_MARKER_NAME, markerPath);

            if (GetFileAttributesA(markerPath) != INVALID_FILE_ATTRIBUTES)
            {
                enabled = 1;
            }
        }
    }

    return enabled == 1;
}

static void log_verbose(const char* fmt, ...)
{
    if (!verbose_logging_enabled())
    {
        return;
    }

    va_list args;
    va_start(args, fmt);
    log_write(fmt, args);
    va_end(args);
}

static void guid_to_string(REFGUID guid, char* out, size_t outSize)
{
    if (!out || outSize == 0)
    {
        return;
    }

    snprintf(
        out,
        outSize,
        "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        (unsigned long)guid.Data1,
        guid.Data2,
        guid.Data3,
        guid.Data4[0],
        guid.Data4[1],
        guid.Data4[2],
        guid.Data4[3],
        guid.Data4[4],
        guid.Data4[5],
        guid.Data4[6],
        guid.Data4[7]
    );
}

static void wide_to_utf8(const WCHAR* input, char* output, int outputSize)
{
    if (!output || outputSize <= 0)
    {
        return;
    }

    output[0] = '\0';

    if (!input)
    {
        return;
    }

    WideCharToMultiByte(
        CP_UTF8,
        0,
        input,
        -1,
        output,
        outputSize,
        NULL,
        NULL
    );

    output[outputSize - 1] = '\0';
}

static const char* yes_no(bool value)
{
    return value ? "YES" : "NO";
}

static const char* hresult_name(HRESULT hr)
{
    if (hr == DI_OK) return "DI_OK";
    if (hr == DI_NOEFFECT) return "DI_NOEFFECT";
    if (hr == DIERR_INVALIDPARAM) return "DIERR_INVALIDPARAM";
    if (hr == DIERR_NOTINITIALIZED) return "DIERR_NOTINITIALIZED";
    if (hr == DIERR_OUTOFMEMORY) return "DIERR_OUTOFMEMORY";
    if (hr == DIERR_UNSUPPORTED) return "DIERR_UNSUPPORTED";
    if (hr == DIERR_NOTACQUIRED) return "DIERR_NOTACQUIRED";
    if (hr == DIERR_INPUTLOST) return "DIERR_INPUTLOST";
    if (hr == DIERR_OTHERAPPHASPRIO) return "DIERR_OTHERAPPHASPRIO_OR_HANDLEEXISTS";
    if (hr == DIERR_DEVICENOTREG) return "DIERR_DEVICENOTREG";
    if (hr == DIERR_OBJECTNOTFOUND) return "DIERR_OBJECTNOTFOUND";
    if (hr == DIERR_EFFECTPLAYING) return "DIERR_EFFECTPLAYING";
    if (hr == DIERR_HASEFFECTS) return "DIERR_HASEFFECTS";
    if (hr == DIERR_INCOMPLETEEFFECT) return "DIERR_INCOMPLETEEFFECT";
    if (hr == DIERR_NOTEXCLUSIVEACQUIRED) return "DIERR_NOTEXCLUSIVEACQUIRED";

    return "UNKNOWN_HRESULT";
}

static const char* device_type_to_string(DWORD type)
{
    DWORD devType = GET_DIDEVICE_TYPE(type);

    switch (devType)
    {
        case DI8DEVTYPE_DEVICE: return "DEVICE";
        case DI8DEVTYPE_MOUSE: return "MOUSE";
        case DI8DEVTYPE_KEYBOARD: return "KEYBOARD";
        case DI8DEVTYPE_JOYSTICK: return "JOYSTICK";
        case DI8DEVTYPE_GAMEPAD: return "GAMEPAD";
        case DI8DEVTYPE_DRIVING: return "DRIVING";
        case DI8DEVTYPE_FLIGHT: return "FLIGHT";
        case DI8DEVTYPE_1STPERSON: return "1STPERSON";
        case DI8DEVTYPE_DEVICECTRL: return "DEVICECTRL";
        case DI8DEVTYPE_SCREENPOINTER: return "SCREENPOINTER";
        case DI8DEVTYPE_REMOTE: return "REMOTE";
        case DI8DEVTYPE_SUPPLEMENTAL: return "SUPPLEMENTAL";
        default: return "UNKNOWN";
    }
}

static const char* devclass_to_string(DWORD devClass)
{
    switch (devClass)
    {
        case DI8DEVCLASS_ALL: return "ALL";
        case DI8DEVCLASS_DEVICE: return "DEVICE";
        case DI8DEVCLASS_POINTER: return "POINTER";
        case DI8DEVCLASS_KEYBOARD: return "KEYBOARD";
        case DI8DEVCLASS_GAMECTRL: return "GAMECTRL";
        default: return "UNKNOWN_CLASS";
    }
}

static const char* effect_guid_to_string(REFGUID guid)
{
    if (IsEqualGUID(guid, GUID_ConstantForce)) return "GUID_ConstantForce";
    if (IsEqualGUID(guid, GUID_RampForce)) return "GUID_RampForce";
    if (IsEqualGUID(guid, GUID_Square)) return "GUID_Square";
    if (IsEqualGUID(guid, GUID_Sine)) return "GUID_Sine";
    if (IsEqualGUID(guid, GUID_Triangle)) return "GUID_Triangle";
    if (IsEqualGUID(guid, GUID_SawtoothUp)) return "GUID_SawtoothUp";
    if (IsEqualGUID(guid, GUID_SawtoothDown)) return "GUID_SawtoothDown";
    if (IsEqualGUID(guid, GUID_Spring)) return "GUID_Spring";
    if (IsEqualGUID(guid, GUID_Damper)) return "GUID_Damper";
    if (IsEqualGUID(guid, GUID_Inertia)) return "GUID_Inertia";
    if (IsEqualGUID(guid, GUID_Friction)) return "GUID_Friction";
    if (IsEqualGUID(guid, GUID_CustomForce)) return "GUID_CustomForce";

    return "UNKNOWN_EFFECT_GUID";
}

static bool name_looks_like_g29(const char* instanceName, const char* productName)
{
    if (!instanceName)
    {
        instanceName = "";
    }

    if (!productName)
    {
        productName = "";
    }

    return
        strstr(instanceName, "G29") != NULL ||
        strstr(productName, "G29") != NULL ||
        strstr(instanceName, "Logitech") != NULL ||
        strstr(productName, "Logitech") != NULL;
}

static bool instance_is_g29_w(const DIDEVICEINSTANCEW* instance)
{
    if (!instance)
    {
        return false;
    }

    char instanceName[512] = {0};
    char productName[512] = {0};

    wide_to_utf8(instance->tszInstanceName, instanceName, sizeof(instanceName));
    wide_to_utf8(instance->tszProductName, productName, sizeof(productName));

    DWORD devType = GET_DIDEVICE_TYPE(instance->dwDevType);

    bool wheelLike =
        devType == DI8DEVTYPE_DRIVING ||
        devType == DI8DEVTYPE_JOYSTICK ||
        devType == DI8DEVTYPE_GAMEPAD;

    return name_looks_like_g29(instanceName, productName) && wheelLike;
}

static bool load_real_dinput8()
{
    if (g_realDInput8 && g_realDirectInput8Create)
    {
        return true;
    }

    char systemDir[MAX_PATH] = {0};
    char dllPath[MAX_PATH] = {0};

    UINT len = GetSystemDirectoryA(systemDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        log_line("GetSystemDirectoryA failed, error=%lu", GetLastError());
        return false;
    }

    lstrcpyA(dllPath, systemDir);
    lstrcatA(dllPath, "\\dinput8.dll");

    log_line("Loading real dinput8 from: %s", dllPath);

    g_realDInput8 = LoadLibraryA(dllPath);
    if (!g_realDInput8)
    {
        log_line("LoadLibraryA failed, error=%lu", GetLastError());
        return false;
    }

    g_realDirectInput8Create =
        (DirectInput8Create_t)GetProcAddress(g_realDInput8, "DirectInput8Create");

    if (!g_realDirectInput8Create)
    {
        log_line("GetProcAddress DirectInput8Create failed, error=%lu", GetLastError());
        return false;
    }

    log_line("Real DirectInput8Create loaded successfully");
    return true;
}


#define PROXY_HELLO_LINE "HELLO source=dinput8_proxy step18"

static SOCKET g_tcpSocket = INVALID_SOCKET;
static bool g_wsaStarted = false;
static bool g_tcpConnectAttempted = false;

// Bumped on every new connection, so a thread holding an old socket value
// never closes a newer connection that happens to reuse the same handle.
static LONG g_tcpGeneration = 0;

// The bridge socket is shared by the game threads sending force feedback and,
// when the virtual G29 is in use, the thread reading wheel input back.
static CRITICAL_SECTION g_tcpLock;

// Caller holds g_tcpLock. The reader thread retries every second, so it asks
// for quiet failures instead of a log line per attempt.
static bool tcp_connect_locked(bool quiet)
{
    if (g_tcpSocket != INVALID_SOCKET)
    {
        return true;
    }

    if (!g_wsaStarted)
    {
        WSADATA wsaData;
        int wsa = WSAStartup(MAKEWORD(2, 2), &wsaData);

        if (wsa != 0)
        {
            log_line("TCP WSAStartup failed code=%d", wsa);
            return false;
        }

        g_wsaStarted = true;
        log_line("TCP WSAStartup ok");
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (s == INVALID_SOCKET)
    {
        log_line("TCP socket failed error=%d", WSAGetLastError());
        return false;
    }

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(54321);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (!quiet)
    {
        log_line("TCP connecting to 127.0.0.1:54321");
    }

    int rc = connect(s, (sockaddr*)&addr, sizeof(addr));

    if (rc == SOCKET_ERROR)
    {
        if (!quiet)
        {
            log_line("TCP connect failed error=%d", WSAGetLastError());
        }

        closesocket(s);
        return false;
    }

    g_tcpSocket = s;
    g_tcpGeneration++;

    log_line("TCP connected to 127.0.0.1:54321 generation=%ld", (long)g_tcpGeneration);
    return true;
}

// Caller holds g_tcpLock.
static bool tcp_connect_once()
{
    if (g_tcpSocket != INVALID_SOCKET)
    {
        return true;
    }

    if (g_tcpConnectAttempted)
    {
        return false;
    }

    g_tcpConnectAttempted = true;

    return tcp_connect_locked(false);
}

// Caller holds g_tcpLock.
static bool tcp_send_raw_locked(const char* line, int len)
{
    if (g_tcpSocket == INVALID_SOCKET)
    {
        return false;
    }

    int sent = send(g_tcpSocket, line, len, 0);

    if (sent == SOCKET_ERROR)
    {
        log_line("TCP send failed error=%d", WSAGetLastError());
        closesocket(g_tcpSocket);
        g_tcpSocket = INVALID_SOCKET;
        g_tcpConnectAttempted = false;
        return false;
    }

    return true;
}

static void tcp_close()
{
    EnterCriticalSection(&g_tcpLock);

    if (g_tcpSocket != INVALID_SOCKET)
    {
        log_line("TCP closing socket");
        closesocket(g_tcpSocket);
        g_tcpSocket = INVALID_SOCKET;
    }

    if (g_wsaStarted)
    {
        WSACleanup();
        g_wsaStarted = false;
    }

    g_tcpConnectAttempted = false;

    LeaveCriticalSection(&g_tcpLock);
}

static void tcp_send_line(const char* fmt, ...)
{
    char line[1024];

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line) - 3, fmt, args);
    va_end(args);

    if (n < 0)
    {
        return;
    }

    line[sizeof(line) - 3] = '\0';
    lstrcatA(line, "\n");

    EnterCriticalSection(&g_tcpLock);

    bool sent = tcp_connect_once() && tcp_send_raw_locked(line, (int)strlen(line));

    LeaveCriticalSection(&g_tcpLock);

    if (sent)
    {
        log_verbose("TCP sent: %s", line);
    }
}

/*
    Wheel input for the virtual G29.

    Since macOS 27 Wine no longer enumerates the G29, so the game would see
    neither the wheel nor its force feedback. The bridge reads the wheel
    through IOHID and, after INPUT_SUBSCRIBE, streams one line per change:

        STATE x=32705 y=255 z=255 rz=255 hat=8 buttons=0x0000000

    Raw HID values: X is the 16-bit steering axis, Y/Z/Rz are the 8-bit
    pedals (255 when released), hat is 0-7 or 8 when centred.
*/
#define WHEEL_AXIS_COUNT 4
#define WHEEL_BUTTON_COUNT 25
#define WHEEL_HAT_CENTERED 8
#define BRIDGE_RETRY_MS 1000

struct WheelInput
{
    LONG axes[WHEEL_AXIS_COUNT];   // X, Y, Z, Rz
    DWORD hat;
    DWORD buttons;
};

// Pedals released, wheel centred, nothing pressed. Used whenever the bridge
// is unreachable, so a lost connection never leaves the throttle held down.
static const WheelInput kNeutralWheelInput = {{32768, 255, 255, 255}, WHEEL_HAT_CENTERED, 0};

static CRITICAL_SECTION g_inputLock;
static WheelInput g_wheelInput = kNeutralWheelInput;
static bool g_bridgeInputLive = false;
static LONG g_readerStarted = 0;
static DWORD g_inputSequence = 0;

// Defined with the virtual device; called with g_inputLock held.
static void virtual_devices_input_changed_locked(const WheelInput& before, const WheelInput& after);

static void input_apply(const WheelInput& next, bool live)
{
    EnterCriticalSection(&g_inputLock);

    WheelInput before = g_wheelInput;
    bool wasLive = g_bridgeInputLive;

    g_wheelInput = next;
    g_bridgeInputLive = live;

    if (memcmp(&before, &next, sizeof(next)) != 0)
    {
        virtual_devices_input_changed_locked(before, next);
    }

    LeaveCriticalSection(&g_inputLock);

    if (live && !wasLive)
    {
        log_line(
            "Bridge input live x=%ld y=%ld z=%ld rz=%ld hat=%lu buttons=0x%07lx",
            (long)next.axes[0],
            (long)next.axes[1],
            (long)next.axes[2],
            (long)next.axes[3],
            (unsigned long)next.hat,
            (unsigned long)next.buttons
        );
    }
    else if (!live && wasLive)
    {
        log_line("Bridge input lost, wheel state set to neutral");
    }
}

static void input_handle_line(const char* line)
{
    unsigned x = 0, y = 0, z = 0, rz = 0, hat = 0, buttons = 0;

    if (sscanf(line, "STATE x=%u y=%u z=%u rz=%u hat=%u buttons=0x%x", &x, &y, &z, &rz, &hat, &buttons) != 6)
    {
        log_line("Bridge sent unexpected line: %s", line);
        return;
    }

    WheelInput next;
    next.axes[0] = (LONG)x;
    next.axes[1] = (LONG)y;
    next.axes[2] = (LONG)z;
    next.axes[3] = (LONG)rz;
    next.hat = hat;
    next.buttons = buttons;

    input_apply(next, true);
}

static DWORD WINAPI bridge_reader_thread(LPVOID)
{
    char recvbuf[2048];
    char line[512];
    size_t lineLen = 0;
    LONG subscribedGeneration = 0;
    DWORD failedAttempts = 0;

    log_line("Bridge reader thread started");

    for (;;)
    {
        EnterCriticalSection(&g_tcpLock);

        SOCKET s = g_tcpSocket;
        LONG generation = g_tcpGeneration;

        // Every connection, including one opened by tcp_send_line after a
        // failed send, has to be subscribed before the bridge sends input.
        if (s != INVALID_SOCKET && generation != subscribedGeneration)
        {
            static const char subscribe[] = PROXY_HELLO_LINE "\nINPUT_SUBSCRIBE\n";

            if (tcp_send_raw_locked(subscribe, (int)(sizeof(subscribe) - 1)))
            {
                subscribedGeneration = generation;
                log_line("Bridge input subscribed generation=%ld", (long)generation);
            }
            else
            {
                s = INVALID_SOCKET;
            }
        }

        LeaveCriticalSection(&g_tcpLock);

        if (s == INVALID_SOCKET)
        {
            input_apply(kNeutralWheelInput, false);
            Sleep(BRIDGE_RETRY_MS);

            EnterCriticalSection(&g_tcpLock);
            bool connected = tcp_connect_locked(true);
            LeaveCriticalSection(&g_tcpLock);

            if (connected)
            {
                failedAttempts = 0;
            }
            else if ((failedAttempts++ % 30) == 0)
            {
                log_line("Bridge not reachable, retrying every %d ms", BRIDGE_RETRY_MS);
            }

            continue;
        }

        int n = recv(s, recvbuf, sizeof(recvbuf), 0);

        if (n <= 0)
        {
            log_line("Bridge input connection closed recv=%d error=%d", n, n < 0 ? WSAGetLastError() : 0);

            EnterCriticalSection(&g_tcpLock);

            if (g_tcpSocket == s && g_tcpGeneration == generation)
            {
                closesocket(s);
                g_tcpSocket = INVALID_SOCKET;
                g_tcpConnectAttempted = false;
            }

            LeaveCriticalSection(&g_tcpLock);

            input_apply(kNeutralWheelInput, false);
            lineLen = 0;
            continue;
        }

        for (int i = 0; i < n; ++i)
        {
            char c = recvbuf[i];

            if (c == '\n')
            {
                line[lineLen] = '\0';

                if (lineLen > 0 && line[lineLen - 1] == '\r')
                {
                    line[lineLen - 1] = '\0';
                }

                if (line[0] != '\0')
                {
                    input_handle_line(line);
                }

                lineLen = 0;
            }
            else if (lineLen + 1 < sizeof(line))
            {
                line[lineLen++] = c;
            }
            else
            {
                lineLen = 0;
            }
        }
    }

    return 0;
}

// Connects to the bridge if needed and starts the input reader once.
// Returns whether the bridge answered, which is what decides whether the
// virtual G29 is offered to the game.
static bool bridge_input_start()
{
    EnterCriticalSection(&g_tcpLock);

    // A failed attempt from an earlier call must not rule out this one: the
    // bridge may have started in the meantime.
    if (g_tcpSocket == INVALID_SOCKET)
    {
        g_tcpConnectAttempted = false;
    }

    bool connected = tcp_connect_once();

    LeaveCriticalSection(&g_tcpLock);

    if (!connected)
    {
        return false;
    }

    if (InterlockedCompareExchange(&g_readerStarted, 1, 0) == 0)
    {
        // The reader never exits, so the DLL must never be unloaded under it.
        HMODULE self = NULL;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            (LPCSTR)&bridge_reader_thread,
            &self
        );

        HANDLE thread = CreateThread(NULL, 0, bridge_reader_thread, NULL, 0, NULL);

        if (thread)
        {
            CloseHandle(thread);
        }
        else
        {
            log_line("Bridge reader CreateThread failed error=%lu", GetLastError());
            InterlockedExchange(&g_readerStarted, 0);
        }
    }

    return true;
}

class FakeDirectInputEffect : public IDirectInputEffect
{
private:
    LONG m_refs;
    GUID m_guid;
    LONG m_lastMagnitude;
    bool m_started;

public:
    FakeDirectInputEffect(REFGUID guid)
        : m_refs(1), m_guid(guid), m_lastMagnitude(0), m_started(false)
    {
        char guidText[64] = {0};
        guid_to_string(guid, guidText, sizeof(guidText));

        log_verbose(
            "FakeEffect created guid=%s name=%s",
            guidText,
            effect_guid_to_string(guid)
        );
    }

    virtual ~FakeDirectInputEffect()
    {
        log_verbose("FakeEffect destroyed");
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)
    {
        char iid[64] = {0};
        guid_to_string(riid, iid, sizeof(iid));

        log_verbose("FakeEffect::QueryInterface riid=%s", iid);

        if (!ppvObject)
        {
            return E_POINTER;
        }

        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IDirectInputEffect))
        {
            *ppvObject = static_cast<IDirectInputEffect*>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        LONG refs = InterlockedIncrement(&m_refs);
        log_verbose("FakeEffect::AddRef refs=%ld", refs);
        return (ULONG)refs;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        LONG refs = InterlockedDecrement(&m_refs);
        log_verbose("FakeEffect::Release refs=%ld", refs);

        if (refs == 0)
        {
            delete this;
            return 0;
        }

        return (ULONG)refs;
    }

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid)
    {
        char guidText[64] = {0};
        guid_to_string(rguid, guidText, sizeof(guidText));

        log_verbose(
            "FakeEffect::Initialize hinst=%p version=0x%08lx guid=%s name=%s",
            hinst,
            (unsigned long)dwVersion,
            guidText,
            effect_guid_to_string(rguid)
        );

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEffectGuid(LPGUID pguid)
    {
        log_verbose("FakeEffect::GetEffectGuid out=%p", pguid);

        if (!pguid)
        {
            return DIERR_INVALIDPARAM;
        }

        *pguid = m_guid;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetParameters(LPDIEFFECT peff, DWORD dwFlags)
    {
        log_verbose("FakeEffect::GetParameters effect=%p flags=0x%08lx", peff, (unsigned long)dwFlags);
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SetParameters(LPCDIEFFECT peff, DWORD dwFlags)
    {
        log_verbose("FakeEffect::SetParameters effect=%p flags=0x%08lx", peff, (unsigned long)dwFlags);

        if (peff)
        {
            log_verbose(
                "FakeEffect::SetParameters details: size=%lu flags=0x%08lx duration=%lu samplePeriod=%lu gain=%lu triggerButton=%lu axes=%lu typeSpecificSize=%lu startDelay=%lu",
                (unsigned long)peff->dwSize,
                (unsigned long)peff->dwFlags,
                (unsigned long)peff->dwDuration,
                (unsigned long)peff->dwSamplePeriod,
                (unsigned long)peff->dwGain,
                (unsigned long)peff->dwTriggerButton,
                (unsigned long)peff->cAxes,
                (unsigned long)peff->cbTypeSpecificParams,
                (unsigned long)peff->dwStartDelay
            );

            if (peff->cAxes > 0 && peff->rgdwAxes)
            {
                for (DWORD i = 0; i < peff->cAxes; ++i)
                {
                    log_verbose(
                        "FakeEffect::SetParameters axis[%lu]=%lu",
                        (unsigned long)i,
                        (unsigned long)peff->rgdwAxes[i]
                    );
                }
            }

            if (peff->cAxes > 0 && peff->rglDirection)
            {
                for (DWORD i = 0; i < peff->cAxes; ++i)
                {
                    log_verbose(
                        "FakeEffect::SetParameters direction[%lu]=%ld",
                        (unsigned long)i,
                        (long)peff->rglDirection[i]
                    );
                }
            }

            if (IsEqualGUID(m_guid, GUID_ConstantForce) &&
                peff->lpvTypeSpecificParams &&
                peff->cbTypeSpecificParams >= sizeof(DICONSTANTFORCE))
            {
                DICONSTANTFORCE* cf = (DICONSTANTFORCE*)peff->lpvTypeSpecificParams;
                m_lastMagnitude = cf->lMagnitude;

                log_verbose(
                    "FakeEffect::SetParameters ConstantForce magnitude=%ld",
                    (long)cf->lMagnitude
                );

                tcp_send_line(
                    "SET_CONSTANT magnitude=%ld",
                    (long)cf->lMagnitude
                );
            }

            if (
                (IsEqualGUID(m_guid, GUID_Spring) ||
                 IsEqualGUID(m_guid, GUID_Damper) ||
                 IsEqualGUID(m_guid, GUID_Friction) ||
                 IsEqualGUID(m_guid, GUID_Inertia)) &&
                peff->lpvTypeSpecificParams &&
                peff->cbTypeSpecificParams >= sizeof(DICONDITION))
            {
                DICONDITION* cond = (DICONDITION*)peff->lpvTypeSpecificParams;

                log_verbose(
                    "FakeEffect::SetParameters Condition offset=%ld positiveCoefficient=%ld negativeCoefficient=%ld positiveSaturation=%lu negativeSaturation=%lu deadBand=%ld",
                    (long)cond->lOffset,
                    (long)cond->lPositiveCoefficient,
                    (long)cond->lNegativeCoefficient,
                    (unsigned long)cond->dwPositiveSaturation,
                    (unsigned long)cond->dwNegativeSaturation,
                    (long)cond->lDeadBand
                );

                tcp_send_line(
                    "SET_CONDITION name=%s offset=%ld positiveCoefficient=%ld negativeCoefficient=%ld positiveSaturation=%lu negativeSaturation=%lu deadBand=%ld",
                    effect_guid_to_string(m_guid),
                    (long)cond->lOffset,
                    (long)cond->lPositiveCoefficient,
                    (long)cond->lNegativeCoefficient,
                    (unsigned long)cond->dwPositiveSaturation,
                    (unsigned long)cond->dwNegativeSaturation,
                    (long)cond->lDeadBand
                );
            }
        }

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Start(DWORD dwIterations, DWORD dwFlags)
    {
        char guidText[64] = {0};
        guid_to_string(m_guid, guidText, sizeof(guidText));

        m_started = true;

        log_verbose(
            "FakeEffect::Start guid=%s name=%s iterations=%lu flags=0x%08lx lastMagnitude=%ld",
            guidText,
            effect_guid_to_string(m_guid),
            (unsigned long)dwIterations,
            (unsigned long)dwFlags,
            (long)m_lastMagnitude
        );

        tcp_send_line(
            "START name=%s iterations=%lu flags=0x%08lx lastMagnitude=%ld",
            effect_guid_to_string(m_guid),
            (unsigned long)dwIterations,
            (unsigned long)dwFlags,
            (long)m_lastMagnitude
        );

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Stop()
    {
        m_started = false;
        log_verbose("FakeEffect::Stop");
        tcp_send_line("STOP");
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEffectStatus(LPDWORD pdwFlags)
    {
        log_verbose("FakeEffect::GetEffectStatus out=%p started=%s", pdwFlags, yes_no(m_started));

        if (pdwFlags)
        {
            *pdwFlags = m_started ? DIEGES_PLAYING : 0;
        }

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Download()
    {
        log_verbose("FakeEffect::Download");
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Unload()
    {
        log_verbose("FakeEffect::Unload");
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE pesc)
    {
        log_verbose("FakeEffect::Escape escape=%p", pesc);
        return DI_OK;
    }
};

/*
    Force feedback of the G29, shared by the proxy around Wine's device and by
    the virtual G29. Wine reports no usable effects for the wheel, so these
    expose only our fake effects, which the bridge turns into HID commands.
*/
static HRESULT g29_create_effect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT* ppdeff)
{
    if (!ppdeff)
    {
        return DIERR_INVALIDPARAM;
    }

    *ppdeff = NULL;

    if (
        IsEqualGUID(rguid, GUID_ConstantForce) ||
        IsEqualGUID(rguid, GUID_Spring) ||
        IsEqualGUID(rguid, GUID_Damper) ||
        IsEqualGUID(rguid, GUID_Friction) ||
        IsEqualGUID(rguid, GUID_Inertia)
    )
    {
        FakeDirectInputEffect* fakeEffect = new FakeDirectInputEffect(rguid);
        *ppdeff = static_cast<IDirectInputEffect*>(fakeEffect);

        if (lpeff)
        {
            fakeEffect->SetParameters(lpeff, DIEP_ALLPARAMS);
        }

        log_line("G29 CreateEffect returning fake effect=%p", *ppdeff);

        tcp_send_line(
            "CREATE_EFFECT name=%s",
            effect_guid_to_string(rguid)
        );

        return DI_OK;
    }

    log_line("G29 CreateEffect unsupported fake guid -> DIERR_UNSUPPORTED");
    return DIERR_UNSUPPORTED;
}

static HRESULT g29_enum_effects(LPDIENUMEFFECTSCALLBACKW lpCallback, LPVOID pvRef)
{
    /*
        Step 11 clean:
        For the G29 we do NOT call Wine's real EnumEffects anymore.
        Wine reports no real FFB effects for this device.
        We expose only our fake DirectInput effects to ETS2.
    */
    log_line("G29 EnumEffects clean fake-only mode active");

    if (!lpCallback)
    {
        log_line("G29 EnumEffects no callback, returning DI_OK");
        return DI_OK;
    }

    static const struct
    {
        const GUID* guid;
        DWORD effType;
        DWORD dynamicParams;
        const WCHAR* name;
    } effects[] = {
        {&GUID_ConstantForce, DIEFT_CONSTANTFORCE, DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_GAIN, L"Constant Force"},
        {&GUID_Spring, DIEFT_CONDITION, DIEP_TYPESPECIFICPARAMS | DIEP_GAIN, L"Spring"},
        {&GUID_Damper, DIEFT_CONDITION, DIEP_TYPESPECIFICPARAMS | DIEP_GAIN, L"Damper"},
        {&GUID_Friction, DIEFT_CONDITION, DIEP_TYPESPECIFICPARAMS | DIEP_GAIN, L"Friction"},
    };

    for (size_t i = 0; i < sizeof(effects) / sizeof(effects[0]); ++i)
    {
        DIEFFECTINFOW info;
        ZeroMemory(&info, sizeof(info));
        info.dwSize = sizeof(info);
        info.guid = *effects[i].guid;
        info.dwEffType = effects[i].effType;
        info.dwStaticParams = DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_DURATION | DIEP_GAIN | DIEP_AXES;
        info.dwDynamicParams = effects[i].dynamicParams;
        lstrcpynW(info.tszName, effects[i].name, MAX_PATH);

        log_line("G29 EnumEffects emitting fake %s", effect_guid_to_string(info.guid));

        if (lpCallback(&info, pvRef) == DIENUM_STOP)
        {
            return DI_OK;
        }
    }

    return DI_OK;
}

static HRESULT g29_get_effect_info(LPDIEFFECTINFOW pdei, REFGUID rguid)
{
    if (!pdei)
    {
        return DIERR_INVALIDPARAM;
    }

    DWORD originalSize = pdei->dwSize;
    ZeroMemory(pdei, sizeof(*pdei));
    pdei->dwSize = originalSize ? originalSize : sizeof(*pdei);
    pdei->guid = rguid;
    pdei->dwStaticParams = DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_DURATION | DIEP_GAIN | DIEP_AXES;
    pdei->dwDynamicParams = DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_GAIN;

    if (IsEqualGUID(rguid, GUID_ConstantForce))
    {
        pdei->dwEffType = DIEFT_CONSTANTFORCE;
        lstrcpynW(pdei->tszName, L"Constant Force", MAX_PATH);
        return DI_OK;
    }

    if (IsEqualGUID(rguid, GUID_Spring))
    {
        pdei->dwEffType = DIEFT_CONDITION;
        lstrcpynW(pdei->tszName, L"Spring", MAX_PATH);
        return DI_OK;
    }

    if (IsEqualGUID(rguid, GUID_Damper))
    {
        pdei->dwEffType = DIEFT_CONDITION;
        lstrcpynW(pdei->tszName, L"Damper", MAX_PATH);
        return DI_OK;
    }

    if (IsEqualGUID(rguid, GUID_Friction))
    {
        pdei->dwEffType = DIEFT_CONDITION;
        lstrcpynW(pdei->tszName, L"Friction", MAX_PATH);
        return DI_OK;
    }

    return DIERR_UNSUPPORTED;
}

static HRESULT g29_send_force_feedback_command(DWORD dwFlags)
{
    tcp_send_line(
        "FF_COMMAND flags=0x%08lx",
        (unsigned long)dwFlags
    );

    return DI_OK;
}


struct EnumObjectsFFContext
{
    LPDIENUMDEVICEOBJECTSCALLBACKW originalCallback;
    LPVOID originalRef;
    DWORD requestedFlags;
    bool isG29;
};

static BOOL CALLBACK enum_objects_ffactuator_callback(
    const DIDEVICEOBJECTINSTANCEW* objectInfo,
    VOID* ref)
{
    EnumObjectsFFContext* ctx = (EnumObjectsFFContext*)ref;

    if (!objectInfo || !ctx)
    {
        return DIENUM_CONTINUE;
    }

    if (!ctx->originalCallback)
    {
        return DIENUM_CONTINUE;
    }

    if (!ctx->isG29)
    {
        return ctx->originalCallback(objectInfo, ctx->originalRef);
    }

    DWORD baseType = DIDFT_GETTYPE(objectInfo->dwType);

    bool isAxis =
        baseType == DIDFT_ABSAXIS ||
        baseType == DIDFT_RELAXIS;

    if (!isAxis)
    {
        return ctx->originalCallback(objectInfo, ctx->originalRef);
    }

    DIDEVICEOBJECTINSTANCEW patchedObject;
    ZeroMemory(&patchedObject, sizeof(patchedObject));
    patchedObject = *objectInfo;

    DWORD oldType = patchedObject.dwType;
    patchedObject.dwType |= DIDFT_FFACTUATOR;

    char name[512] = {0};
    char guidText[64] = {0};

    wide_to_utf8(patchedObject.tszName, name, sizeof(name));
    guid_to_string(patchedObject.guidType, guidText, sizeof(guidText));

    log_line(
        "Step14 EnumObjects patched axis as FFACTUATOR name='%s' guidType=%s dwOfs=%lu oldType=0x%08lx newType=0x%08lx requestedFlags=0x%08lx",
        name,
        guidText,
        (unsigned long)patchedObject.dwOfs,
        (unsigned long)oldType,
        (unsigned long)patchedObject.dwType,
        (unsigned long)ctx->requestedFlags
    );

    return ctx->originalCallback(&patchedObject, ctx->originalRef);
}

class DirectInputDevice8WProxy : public IDirectInputDevice8W
{
private:
    IDirectInputDevice8W* m_real;
    LONG m_refs;
    bool m_isG29;

public:
    DirectInputDevice8WProxy(IDirectInputDevice8W* real, bool isG29)
        : m_real(real), m_refs(1), m_isG29(isG29)
    {
        log_line("DeviceProxy created real=%p isG29=%s", m_real, yes_no(m_isG29));
    }

    virtual ~DirectInputDevice8WProxy()
    {
        log_line("DeviceProxy destroyed");
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)
    {
        char iid[64] = {0};
        guid_to_string(riid, iid, sizeof(iid));

        log_line("DeviceProxy::QueryInterface riid=%s", iid);

        if (!ppvObject)
        {
            return E_POINTER;
        }

        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IDirectInputDevice8W))
        {
            *ppvObject = static_cast<IDirectInputDevice8W*>(this);
            AddRef();
            return S_OK;
        }

        return m_real->QueryInterface(riid, ppvObject);
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        LONG refs = InterlockedIncrement(&m_refs);
        log_line("DeviceProxy::AddRef refs=%ld", refs);
        return (ULONG)refs;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        LONG refs = InterlockedDecrement(&m_refs);
        log_line("DeviceProxy::Release refs=%ld", refs);

        if (refs == 0)
        {
            ULONG realRefs = m_real->Release();
            log_line("DeviceProxy::Release released real refs=%lu", realRefs);
            delete this;
            return 0;
        }

        return (ULONG)refs;
    }

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS lpDIDevCaps)
    {
        log_line("DeviceProxy::GetCapabilities caps=%p isG29=%s", lpDIDevCaps, yes_no(m_isG29));

        HRESULT hr = m_real->GetCapabilities(lpDIDevCaps);

        if (SUCCEEDED(hr) && lpDIDevCaps && m_isG29)
        {
            DWORD oldFlags = lpDIDevCaps->dwFlags;

            lpDIDevCaps->dwFlags |= DIDC_FORCEFEEDBACK;
            lpDIDevCaps->dwFlags |= DIDC_FFFADE;
            lpDIDevCaps->dwFlags |= DIDC_FFATTACK;
            lpDIDevCaps->dwFlags |= DIDC_POSNEGCOEFFICIENTS;
            lpDIDevCaps->dwFlags |= DIDC_POSNEGSATURATION;
            lpDIDevCaps->dwFlags |= DIDC_SATURATION;

            if (lpDIDevCaps->dwFFSamplePeriod == 0)
            {
                lpDIDevCaps->dwFFSamplePeriod = 1000;
            }

            if (lpDIDevCaps->dwFFMinTimeResolution == 0)
            {
                lpDIDevCaps->dwFFMinTimeResolution = 1000;
            }

            if (lpDIDevCaps->dwFFDriverVersion == 0)
            {
                lpDIDevCaps->dwFFDriverVersion = 1;
            }

            log_line(
                "DeviceProxy::GetCapabilities patched oldFlags=0x%08lx newFlags=0x%08lx FORCEFEEDBACK=%s axes=%lu buttons=%lu povs=%lu ffSamplePeriod=%lu ffMinTimeResolution=%lu ffDriver=%lu",
                (unsigned long)oldFlags,
                (unsigned long)lpDIDevCaps->dwFlags,
                yes_no((lpDIDevCaps->dwFlags & DIDC_FORCEFEEDBACK) != 0),
                (unsigned long)lpDIDevCaps->dwAxes,
                (unsigned long)lpDIDevCaps->dwButtons,
                (unsigned long)lpDIDevCaps->dwPOVs,
                (unsigned long)lpDIDevCaps->dwFFSamplePeriod,
                (unsigned long)lpDIDevCaps->dwFFMinTimeResolution,
                (unsigned long)lpDIDevCaps->dwFFDriverVersion
            );

            return hr;
        }

        if (lpDIDevCaps)
        {
            log_line(
                "DeviceProxy::GetCapabilities real hr=0x%08lx %s flags=0x%08lx FORCEFEEDBACK=%s axes=%lu buttons=%lu povs=%lu",
                (unsigned long)hr,
                hresult_name(hr),
                (unsigned long)lpDIDevCaps->dwFlags,
                yes_no((lpDIDevCaps->dwFlags & DIDC_FORCEFEEDBACK) != 0),
                (unsigned long)lpDIDevCaps->dwAxes,
                (unsigned long)lpDIDevCaps->dwButtons,
                (unsigned long)lpDIDevCaps->dwPOVs
            );
        }
        else
        {
            log_line("DeviceProxy::GetCapabilities real hr=0x%08lx %s", (unsigned long)hr, hresult_name(hr));
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags)
    {
        log_line(
            "DeviceProxy::EnumObjects begin callback=%p ref=%p flags=0x%08lx isG29=%s",
            lpCallback,
            pvRef,
            (unsigned long)dwFlags,
            yes_no(m_isG29)
        );

        EnumObjectsFFContext ctx;
        ZeroMemory(&ctx, sizeof(ctx));
        ctx.originalCallback = lpCallback;
        ctx.originalRef = pvRef;
        ctx.requestedFlags = dwFlags;
        ctx.isG29 = m_isG29;

        HRESULT hr = m_real->EnumObjects(
            enum_objects_ffactuator_callback,
            &ctx,
            dwFlags
        );

        log_line(
            "DeviceProxy::EnumObjects end hr=0x%08lx %s isG29=%s",
            (unsigned long)hr,
            hresult_name(hr),
            yes_no(m_isG29)
        );

        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID rguidProp, LPDIPROPHEADER pdiph)
    {
        return m_real->GetProperty(rguidProp, pdiph);
    }

    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID rguidProp, LPCDIPROPHEADER pdiph)
    {
        return m_real->SetProperty(rguidProp, pdiph);
    }

    HRESULT STDMETHODCALLTYPE Acquire()
    {
        log_line("DeviceProxy::Acquire isG29=%s", yes_no(m_isG29));

        HRESULT hr = m_real->Acquire();

        if (m_isG29 && SUCCEEDED(hr))
        {
            tcp_send_line(PROXY_HELLO_LINE);
            tcp_send_line("ACQUIRE");
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE Unacquire()
    {
        log_line("DeviceProxy::Unacquire isG29=%s", yes_no(m_isG29));

        if (m_isG29)
        {
            tcp_send_line("UNACQUIRE");
        }

        return m_real->Unacquire();
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cbData, LPVOID lpvData)
    {
        return m_real->GetDeviceState(cbData, lpvData);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags)
    {
        return m_real->GetDeviceData(cbObjectData, rgdod, pdwInOut, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT lpdf)
    {
        log_line("DeviceProxy::SetDataFormat format=%p isG29=%s", lpdf, yes_no(m_isG29));
        return m_real->SetDataFormat(lpdf);
    }

    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE hEvent)
    {
        return m_real->SetEventNotification(hEvent);
    }

    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hwnd, DWORD dwFlags)
    {
        log_line(
            "DeviceProxy::SetCooperativeLevel hwnd=%p flags=0x%08lx isG29=%s",
            hwnd,
            (unsigned long)dwFlags,
            yes_no(m_isG29)
        );

        return m_real->SetCooperativeLevel(hwnd, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW pdidoi, DWORD dwObj, DWORD dwHow)
    {
        HRESULT hr = m_real->GetObjectInfo(pdidoi, dwObj, dwHow);

        if (m_isG29)
        {
            if (SUCCEEDED(hr) && pdidoi)
            {
                DWORD baseType = DIDFT_GETTYPE(pdidoi->dwType);

                bool isAxis =
                    baseType == DIDFT_ABSAXIS ||
                    baseType == DIDFT_RELAXIS;

                if (isAxis)
                {
                    DWORD oldType = pdidoi->dwType;
                    DWORD oldFlags = pdidoi->dwFlags;

                    pdidoi->dwType |= DIDFT_FFACTUATOR;
                    pdidoi->dwFlags |= DIDOI_FFACTUATOR;

                    char name[512] = {0};
                    wide_to_utf8(pdidoi->tszName, name, sizeof(name));

                    log_line(
                        "Step16 GetObjectInfo FFACTUATOR injected obj=0x%lx how=0x%lx name='%s' oldType=0x%08lx newType=0x%08lx oldFlags=0x%08lx newFlags=0x%08lx",
                        (unsigned long)dwObj,
                        (unsigned long)dwHow,
                        name,
                        (unsigned long)oldType,
                        (unsigned long)pdidoi->dwType,
                        (unsigned long)oldFlags,
                        (unsigned long)pdidoi->dwFlags
                    );
                }
                else
                {
                    log_line(
                        "Step15 GetObjectInfo G29 non-axis obj=0x%lx how=0x%lx type=0x%08lx flags=0x%08lx hr=0x%08lx",
                        (unsigned long)dwObj,
                        (unsigned long)dwHow,
                        (unsigned long)pdidoi->dwType,
                        (unsigned long)pdidoi->dwFlags,
                        (unsigned long)hr
                    );
                }
            }
            else
            {
                log_line(
                    "Step15 GetObjectInfo G29 hr=0x%08lx obj=0x%lx how=0x%lx out=%p",
                    (unsigned long)hr,
                    (unsigned long)dwObj,
                    (unsigned long)dwHow,
                    pdidoi
                );
            }
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW pdidi)
    {
        HRESULT hr = m_real->GetDeviceInfo(pdidi);

        if (SUCCEEDED(hr) && pdidi)
        {
            char instanceName[512] = {0};
            char productName[512] = {0};

            wide_to_utf8(pdidi->tszInstanceName, instanceName, sizeof(instanceName));
            wide_to_utf8(pdidi->tszProductName, productName, sizeof(productName));

            log_line(
                "DeviceProxy::GetDeviceInfo hr=0x%08lx instance='%s' product='%s' type=0x%08lx typeName=%s",
                (unsigned long)hr,
                instanceName,
                productName,
                (unsigned long)pdidi->dwDevType,
                device_type_to_string(pdidi->dwDevType)
            );
        }
        else
        {
            log_line("DeviceProxy::GetDeviceInfo hr=0x%08lx %s", (unsigned long)hr, hresult_name(hr));
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags)
    {
        return m_real->RunControlPanel(hwndOwner, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid)
    {
        return m_real->Initialize(hinst, dwVersion, rguid);
    }

    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT* ppdeff, LPUNKNOWN punkOuter)
    {
        char guidText[64] = {0};
        guid_to_string(rguid, guidText, sizeof(guidText));

        log_line(
            "DeviceProxy::CreateEffect guid=%s name=%s effect=%p out=%p outer=%p isG29=%s",
            guidText,
            effect_guid_to_string(rguid),
            lpeff,
            ppdeff,
            punkOuter,
            yes_no(m_isG29)
        );

        if (m_isG29)
        {
            return g29_create_effect(rguid, lpeff, ppdeff);
        }

        HRESULT hr = m_real->CreateEffect(rguid, lpeff, ppdeff, punkOuter);

        log_line(
            "DeviceProxy::CreateEffect real hr=0x%08lx %s effect=%p",
            (unsigned long)hr,
            hresult_name(hr),
            ppdeff ? *ppdeff : NULL
        );

        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwEffType)
    {
        log_line(
            "DeviceProxy::EnumEffects callback=%p ref=%p effType=0x%08lx isG29=%s",
            lpCallback,
            pvRef,
            (unsigned long)dwEffType,
            yes_no(m_isG29)
        );

        if (!m_isG29)
        {
            HRESULT hr = m_real->EnumEffects(lpCallback, pvRef, dwEffType);
            log_line("DeviceProxy::EnumEffects real hr=0x%08lx %s", (unsigned long)hr, hresult_name(hr));
            return hr;
        }

        return g29_enum_effects(lpCallback, pvRef);
    }

    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW pdei, REFGUID rguid)
    {
        char guidText[64] = {0};
        guid_to_string(rguid, guidText, sizeof(guidText));

        log_line(
            "DeviceProxy::GetEffectInfo guid=%s name=%s out=%p isG29=%s",
            guidText,
            effect_guid_to_string(rguid),
            pdei,
            yes_no(m_isG29)
        );

        if (m_isG29)
        {
            return g29_get_effect_info(pdei, rguid);
        }

        HRESULT hr = m_real->GetEffectInfo(pdei, rguid);
        log_line("DeviceProxy::GetEffectInfo real hr=0x%08lx %s", (unsigned long)hr, hresult_name(hr));
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD pdwOut)
    {
        log_line("DeviceProxy::GetForceFeedbackState out=%p isG29=%s", pdwOut, yes_no(m_isG29));

        if (m_isG29)
        {
            if (pdwOut)
            {
                *pdwOut = 0;
            }

            return DI_OK;
        }

        HRESULT hr = m_real->GetForceFeedbackState(pdwOut);

        log_line(
            "DeviceProxy::GetForceFeedbackState real hr=0x%08lx %s state=0x%08lx",
            (unsigned long)hr,
            hresult_name(hr),
            pdwOut ? (unsigned long)*pdwOut : 0UL
        );

        return hr;
    }

    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD dwFlags)
    {
        log_line("DeviceProxy::SendForceFeedbackCommand flags=0x%08lx isG29=%s", (unsigned long)dwFlags, yes_no(m_isG29));

        if (m_isG29)
        {
            return g29_send_force_feedback_command(dwFlags);
        }

        HRESULT hr = m_real->SendForceFeedbackCommand(dwFlags);

        log_line("DeviceProxy::SendForceFeedbackCommand real hr=0x%08lx %s", (unsigned long)hr, hresult_name(hr));

        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl)
    {
        log_line("DeviceProxy::EnumCreatedEffectObjects callback=%p ref=%p flags=0x%08lx isG29=%s", lpCallback, pvRef, (unsigned long)fl, yes_no(m_isG29));

        if (m_isG29)
        {
            return DI_OK;
        }

        HRESULT hr = m_real->EnumCreatedEffectObjects(lpCallback, pvRef, fl);

        log_line("DeviceProxy::EnumCreatedEffectObjects real hr=0x%08lx %s", (unsigned long)hr, hresult_name(hr));

        return hr;
    }

    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE pesc)
    {
        return m_real->Escape(pesc);
    }

    HRESULT STDMETHODCALLTYPE Poll()
    {
        return m_real->Poll();
    }

    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl)
    {
        return m_real->SendDeviceData(cbObjectData, rgdod, pdwInOut, fl);
    }

    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR lpszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags)
    {
        log_line("DeviceProxy::EnumEffectsInFile callback=%p ref=%p flags=0x%08lx isG29=%s", pec, pvRef, (unsigned long)dwFlags, yes_no(m_isG29));

        if (m_isG29)
        {
            return DIERR_UNSUPPORTED;
        }

        HRESULT hr = m_real->EnumEffectsInFile(lpszFileName, pec, pvRef, dwFlags);

        log_line("DeviceProxy::EnumEffectsInFile real hr=0x%08lx %s", (unsigned long)hr, hresult_name(hr));

        return hr;
    }

    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR lpszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags)
    {
        return m_real->WriteEffectToFile(lpszFileName, dwEntries, rgDiFileEft, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW lpdiaf, LPCWSTR lpszUserName, DWORD dwFlags)
    {
        return m_real->BuildActionMap(lpdiaf, lpszUserName, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW lpdiActionFormat, LPCWSTR lptszUserName, DWORD dwFlags)
    {
        return m_real->SetActionMap(lpdiActionFormat, lptszUserName, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW lpdiDevImageInfoHeader)
    {
        return m_real->GetImageInfo(lpdiDevImageInfoHeader);
    }
};

/*
    Virtual G29.

    Offered by EnumDevices only when Wine enumerated no G29 and the bridge
    answers. It carries the identity Wine used to report for the wheel, so
    games keep recognising it, while the objects follow what Windows builds
    from the G29 HID descriptor: X steering, Y/Z/Rz pedals, one hat, 25
    buttons. Raw values pass through unchanged, as on Windows.
*/
static const GUID kWineG29InstanceGuid =
    {0x9E573EDF, 0x7734, 0x11D2, {0x8D, 0x4A, 0x23, 0x90, 0x3F, 0xB6, 0xBD, 0xF7}};

// Used instead when Wine has already given the GUID above to another device.
static const GUID kCrossFFBInstanceGuid =
    {0xC24F046D, 0xF00D, 0x0002, {0x90, 0x29, 0x56, 0x49, 0x52, 0x54, 0x55, 0x41}};

static const GUID kG29ProductGuid =
    {0xC24F046D, 0x0000, 0x0000, {0x00, 0x00, 'P', 'I', 'D', 'V', 'I', 'D'}};

/*
    Step 13: a stable fake non-null guidFFDriver. It does not need to
    correspond to a real Windows FF driver, because CreateEffect is handled
    by the proxy.
*/
static const GUID kG29FFDriverGuid =
    {0xC24F046D, 0xF00D, 0x0001, {0x90, 0x29, 0x47, 0x32, 0x39, 0x46, 0x46, 0x42}};

static const GUID kHIDClassGuid =
    {0x745A17A0, 0x74D3, 0x11D0, {0xB6, 0xFE, 0x00, 0xA0, 0xC9, 0x0F, 0x57, 0xDA}};

#define G29_VENDOR_ID 0x046D
#define G29_PRODUCT_ID 0xC24F
#define G29_DEVICE_TYPE 0x00010116   // DRIVING, as Wine reported the G29
#define G29_PRODUCT_NAME L"Logitech G29 Driving Force Racing Wheel"
#define G29_TYPE_NAME L"VID_046D&PID_C24F"
#define G29_DEVICE_PATH L"\\\\?\\hid#vid_046d&pid_c24f#crossffb&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"

#define VIRTUAL_OBJECT_COUNT (WHEEL_AXIS_COUNT + 1 + WHEEL_BUTTON_COUNT)
#define VIRTUAL_MAX_DEVICES 8
#define VIRTUAL_MAX_BUFFER 8192
#define VIRTUAL_MAX_EXTRA_POVS 8
#define VIRTUAL_STATE_LOG_EVERY 3600

// Predefined DirectInput properties are small integers disguised as GUID
// references and must be compared by address, never dereferenced.
#define DIPROP_ID(ref) ((ULONG_PTR)&(ref))

static GUID g_virtualInstanceGuid = kWineG29InstanceGuid;
static bool g_virtualGuidChosen = false;
static bool g_virtualOffered = false;
static bool g_realG29Seen = false;

enum VirtualObjectKind
{
    VOBJ_AXIS,
    VOBJ_POV,
    VOBJ_BUTTON
};

struct VirtualObject
{
    VirtualObjectKind kind;
    int index;              // axis 0-3 or button 0-24
    const GUID* guidType;
    DWORD type;
    DWORD flags;
    WORD usagePage;
    WORD usage;
    DWORD nativeOffset;     // offset in c_dfDIJoystick2
    LONG logicalMax;
};

struct VirtualObjectTable
{
    VirtualObject items[VIRTUAL_OBJECT_COUNT];

    VirtualObjectTable()
    {
        static const GUID* const axisGuids[WHEEL_AXIS_COUNT] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RzAxis};
        static const WORD axisUsages[WHEEL_AXIS_COUNT] = {0x30, 0x31, 0x32, 0x35};
        static const DWORD axisOffsets[WHEEL_AXIS_COUNT] = {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RZ};
        static const LONG axisMax[WHEEL_AXIS_COUNT] = {65535, 255, 255, 255};

        int n = 0;

        for (int i = 0; i < WHEEL_AXIS_COUNT; ++i, ++n)
        {
            // Every axis is an FF actuator, as Step 14/16 reported them.
            items[n].kind = VOBJ_AXIS;
            items[n].index = i;
            items[n].guidType = axisGuids[i];
            items[n].type = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i) | DIDFT_FFACTUATOR;
            items[n].flags = DIDOI_ASPECTPOSITION | DIDOI_FFACTUATOR;
            items[n].usagePage = 0x01;
            items[n].usage = axisUsages[i];
            items[n].nativeOffset = axisOffsets[i];
            items[n].logicalMax = axisMax[i];
        }

        items[n].kind = VOBJ_POV;
        items[n].index = 0;
        items[n].guidType = &GUID_POV;
        items[n].type = DIDFT_POV | DIDFT_MAKEINSTANCE(0);
        items[n].flags = 0;
        items[n].usagePage = 0x01;
        items[n].usage = 0x39;
        items[n].nativeOffset = DIJOFS_POV(0);
        items[n].logicalMax = 7;
        ++n;

        for (int i = 0; i < WHEEL_BUTTON_COUNT; ++i, ++n)
        {
            items[n].kind = VOBJ_BUTTON;
            items[n].index = i;
            items[n].guidType = &GUID_Button;
            items[n].type = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(i);
            items[n].flags = 0;
            items[n].usagePage = 0x09;
            items[n].usage = (WORD)(i + 1);
            items[n].nativeOffset = DIJOFS_BUTTON(i);
            items[n].logicalMax = 1;
        }
    }
};

static const VirtualObject* virtual_objects()
{
    static const VirtualObjectTable table;
    return table.items;
}

static void virtual_object_name(const VirtualObject& o, WCHAR* out, size_t count)
{
    static const WCHAR* const axisNames[WHEEL_AXIS_COUNT] = {L"X Axis", L"Y Axis", L"Z Axis", L"Z Rotation"};

    switch (o.kind)
    {
        case VOBJ_AXIS:
            lstrcpynW(out, axisNames[o.index], (int)count);
            break;

        case VOBJ_POV:
            lstrcpynW(out, L"Hat Switch", (int)count);
            break;

        case VOBJ_BUTTON:
            _snwprintf(out, count, L"Button %d", o.index);
            out[count - 1] = L'\0';
            break;
    }
}

static HRESULT fill_object_instance(const VirtualObject& o, DWORD ofs, LPDIDEVICEOBJECTINSTANCEW out)
{
    DWORD size = out->dwSize;

    if (size != sizeof(DIDEVICEOBJECTINSTANCEW) && size != sizeof(DIDEVICEOBJECTINSTANCE_DX3W))
    {
        return DIERR_INVALIDPARAM;
    }

    DIDEVICEOBJECTINSTANCEW full;
    ZeroMemory(&full, sizeof(full));
    full.dwSize = size;
    full.guidType = *o.guidType;
    full.dwOfs = ofs;
    full.dwType = o.type;
    full.dwFlags = o.flags;
    virtual_object_name(o, full.tszName, MAX_PATH);
    full.wUsagePage = o.usagePage;
    full.wUsage = o.usage;

    // The DX3 layout is a prefix of the full one.
    memcpy(out, &full, size);
    return DI_OK;
}

static HRESULT fill_virtual_instance(LPDIDEVICEINSTANCEW out)
{
    DWORD size = out->dwSize;

    if (size != sizeof(DIDEVICEINSTANCEW) && size != sizeof(DIDEVICEINSTANCE_DX3W))
    {
        return DIERR_INVALIDPARAM;
    }

    DIDEVICEINSTANCEW full;
    ZeroMemory(&full, sizeof(full));
    full.dwSize = size;
    full.guidInstance = g_virtualInstanceGuid;
    full.guidProduct = kG29ProductGuid;
    full.dwDevType = G29_DEVICE_TYPE;
    lstrcpynW(full.tszInstanceName, G29_PRODUCT_NAME, MAX_PATH);
    lstrcpynW(full.tszProductName, G29_PRODUCT_NAME, MAX_PATH);
    full.guidFFDriver = kG29FFDriverGuid;
    full.wUsagePage = 0x01;
    full.wUsage = 0x04;

    memcpy(out, &full, size);
    return DI_OK;
}

static bool object_matches_enum_filter(const VirtualObject& o, DWORD flags)
{
    if (flags == DIDFT_ALL)
    {
        return true;
    }

    DWORD typeFilter = DIDFT_GETTYPE(flags);

    if (typeFilter && !(typeFilter & DIDFT_GETTYPE(o.type)))
    {
        return false;
    }

    DWORD attrFilter = flags & (DIDFT_FFACTUATOR | DIDFT_FFEFFECTTRIGGER | DIDFT_OUTPUT | DIDFT_VENDORDEFINED | DIDFT_ALIAS);

    return (o.type & attrFilter) == attrFilter;
}

// The same matching DirectInput applies to a DIOBJECTDATAFORMAT entry: GUID
// when given, object type, and instance unless it is DIDFT_ANYINSTANCE.
static bool object_matches_format(const VirtualObject& o, const DIOBJECTDATAFORMAT& od)
{
    if (od.pguid && !IsEqualGUID(*od.pguid, *o.guidType))
    {
        return false;
    }

    DWORD typeFilter = DIDFT_GETTYPE(od.dwType);

    if (typeFilter && !(typeFilter & DIDFT_GETTYPE(o.type)))
    {
        return false;
    }

    WORD instance = DIDFT_GETINSTANCE(od.dwType);

    if (instance != 0xFFFF && instance != DIDFT_GETINSTANCE(o.type))
    {
        return false;
    }

    if ((od.dwType & DIDFT_FFACTUATOR) && !(o.type & DIDFT_FFACTUATOR))
    {
        return false;
    }

    return true;
}

class VirtualG29Device;

// Registered virtual devices, protected by g_inputLock.
static VirtualG29Device* g_virtualDevices[VIRTUAL_MAX_DEVICES];

class VirtualG29Device : public IDirectInputDevice8W
{
private:
    LONG m_refs;

    // Everything below is protected by g_inputLock: the reader thread
    // queues buffered events while the game reads state.
    bool m_acquired;
    bool m_formatSet;
    DWORD m_dataSize;
    LONG m_userOffset[VIRTUAL_OBJECT_COUNT];
    DWORD m_extraPovOffsets[VIRTUAL_MAX_EXTRA_POVS];
    DWORD m_extraPovCount;
    LONG m_rangeMin[WHEEL_AXIS_COUNT];
    LONG m_rangeMax[WHEEL_AXIS_COUNT];
    DWORD m_deadzone[WHEEL_AXIS_COUNT];
    DWORD m_saturation[WHEEL_AXIS_COUNT];
    DIDEVICEOBJECTDATA* m_buffer;
    DWORD m_bufferSize;
    DWORD m_bufferHead;
    DWORD m_bufferCount;
    bool m_overflow;
    HANDLE m_event;
    DWORD m_ffGain;
    DWORD m_autocenter;
    DWORD m_stateReads;

    LONG axis_value_locked(int axis, LONG raw) const
    {
        LONG logicalMax = virtual_objects()[axis].logicalMax;

        if (raw < 0)
        {
            raw = 0;
        }

        if (raw > logicalMax)
        {
            raw = logicalMax;
        }

        double t = (double)raw / (double)logicalMax;

        DWORD deadzone = m_deadzone[axis];
        DWORD saturation = m_saturation[axis];

        // Dead zone and saturation apply around the centre of the axis.
        if (deadzone > 0 || saturation < 10000)
        {
            double c = t * 2.0 - 1.0;
            double a = c < 0.0 ? -c : c;
            double d = deadzone / 10000.0;
            double s = saturation / 10000.0;

            if (a <= d)
            {
                a = 0.0;
            }
            else if (a >= s || s <= d)
            {
                a = 1.0;
            }
            else
            {
                a = (a - d) / (s - d);
            }

            c = c < 0.0 ? -a : a;
            t = (c + 1.0) / 2.0;
        }

        double value = (double)m_rangeMin[axis] + t * ((double)m_rangeMax[axis] - (double)m_rangeMin[axis]);

        return (LONG)(value < 0.0 ? value - 0.5 : value + 0.5);
    }

    DWORD object_data_locked(int i, const WheelInput& input) const
    {
        const VirtualObject& o = virtual_objects()[i];

        switch (o.kind)
        {
            case VOBJ_AXIS:
                return (DWORD)axis_value_locked(o.index, input.axes[o.index]);

            case VOBJ_POV:
                return input.hat < WHEEL_HAT_CENTERED ? input.hat * 4500 : 0xFFFFFFFF;

            case VOBJ_BUTTON:
                return ((input.buttons >> o.index) & 1) ? 0x80 : 0x00;
        }

        return 0;
    }

    static bool object_changed(int i, const WheelInput& before, const WheelInput& after)
    {
        const VirtualObject& o = virtual_objects()[i];

        switch (o.kind)
        {
            case VOBJ_AXIS:
                return before.axes[o.index] != after.axes[o.index];

            case VOBJ_POV:
                return before.hat != after.hat;

            case VOBJ_BUTTON:
                return ((before.buttons ^ after.buttons) >> o.index) & 1;
        }

        return false;
    }

    DWORD reported_offset_locked(int i) const
    {
        if (m_formatSet && m_userOffset[i] >= 0)
        {
            return (DWORD)m_userOffset[i];
        }

        return virtual_objects()[i].nativeOffset;
    }

    int find_object_locked(DWORD obj, DWORD how) const
    {
        const VirtualObject* objects = virtual_objects();

        for (int i = 0; i < VIRTUAL_OBJECT_COUNT; ++i)
        {
            switch (how)
            {
                case DIPH_BYOFFSET:
                    if (m_formatSet && m_userOffset[i] < 0)
                    {
                        continue;
                    }

                    if (reported_offset_locked(i) == obj)
                    {
                        return i;
                    }
                    break;

                case DIPH_BYID:
                    if ((DIDFT_GETTYPE(obj) & DIDFT_GETTYPE(objects[i].type)) &&
                        DIDFT_GETINSTANCE(obj) == DIDFT_GETINSTANCE(objects[i].type))
                    {
                        return i;
                    }
                    break;

                case DIPH_BYUSAGE:
                    if (LOWORD(obj) == objects[i].usage && HIWORD(obj) == objects[i].usagePage)
                    {
                        return i;
                    }
                    break;
            }
        }

        return -1;
    }

    void push_event_locked(DWORD ofs, DWORD data)
    {
        if (m_bufferCount == m_bufferSize)
        {
            // Keep the newest input: for a wheel the latest position matters.
            m_overflow = true;
            m_bufferHead = (m_bufferHead + 1) % m_bufferSize;
            m_bufferCount--;
        }

        DIDEVICEOBJECTDATA& e = m_buffer[(m_bufferHead + m_bufferCount) % m_bufferSize];
        ZeroMemory(&e, sizeof(e));
        e.dwOfs = ofs;
        e.dwData = data;
        e.dwTimeStamp = GetTickCount();
        e.dwSequence = ++g_inputSequence;

        m_bufferCount++;
    }

    HRESULT get_property_locked(ULONG_PTR prop, LPDIPROPHEADER pdiph) const
    {
        const VirtualObject* objects = virtual_objects();
        int obj = -1;

        if (pdiph->dwHow != DIPH_DEVICE)
        {
            obj = find_object_locked(pdiph->dwObj, pdiph->dwHow);

            if (obj < 0)
            {
                return DIERR_OBJECTNOTFOUND;
            }
        }

        int axis = (obj >= 0 && objects[obj].kind == VOBJ_AXIS) ? objects[obj].index : -1;

        if (prop == DIPROP_ID(DIPROP_RANGE) ||
            prop == DIPROP_ID(DIPROP_LOGICALRANGE) ||
            prop == DIPROP_ID(DIPROP_PHYSICALRANGE))
        {
            if (pdiph->dwSize != sizeof(DIPROPRANGE))
            {
                return DIERR_INVALIDPARAM;
            }

            if (axis < 0)
            {
                return DIERR_UNSUPPORTED;
            }

            LPDIPROPRANGE range = (LPDIPROPRANGE)pdiph;

            if (prop == DIPROP_ID(DIPROP_RANGE))
            {
                range->lMin = m_rangeMin[axis];
                range->lMax = m_rangeMax[axis];
            }
            else
            {
                range->lMin = 0;
                range->lMax = objects[obj].logicalMax;
            }

            return DI_OK;
        }

        if (prop == DIPROP_ID(DIPROP_GUIDANDPATH))
        {
            if (pdiph->dwSize != sizeof(DIPROPGUIDANDPATH))
            {
                return DIERR_INVALIDPARAM;
            }

            LPDIPROPGUIDANDPATH path = (LPDIPROPGUIDANDPATH)pdiph;
            path->guidClass = kHIDClassGuid;
            lstrcpynW(path->wszPath, G29_DEVICE_PATH, MAX_PATH);
            return DI_OK;
        }

        if (prop == DIPROP_ID(DIPROP_INSTANCENAME) ||
            prop == DIPROP_ID(DIPROP_PRODUCTNAME) ||
            prop == DIPROP_ID(DIPROP_TYPENAME))
        {
            if (pdiph->dwSize != sizeof(DIPROPSTRING))
            {
                return DIERR_INVALIDPARAM;
            }

            LPDIPROPSTRING text = (LPDIPROPSTRING)pdiph;
            lstrcpynW(text->wsz, prop == DIPROP_ID(DIPROP_TYPENAME) ? G29_TYPE_NAME : G29_PRODUCT_NAME, MAX_PATH);
            return DI_OK;
        }

        DWORD value = 0;

        if (prop == DIPROP_ID(DIPROP_BUFFERSIZE))
        {
            value = m_bufferSize;
        }
        else if (prop == DIPROP_ID(DIPROP_AXISMODE))
        {
            value = DIPROPAXISMODE_ABS;
        }
        else if (prop == DIPROP_ID(DIPROP_GRANULARITY))
        {
            value = 1;
        }
        else if (prop == DIPROP_ID(DIPROP_DEADZONE) || prop == DIPROP_ID(DIPROP_SATURATION))
        {
            if (axis < 0)
            {
                return DIERR_UNSUPPORTED;
            }

            value = prop == DIPROP_ID(DIPROP_DEADZONE) ? m_deadzone[axis] : m_saturation[axis];
        }
        else if (prop == DIPROP_ID(DIPROP_CALIBRATIONMODE))
        {
            value = DIPROPCALIBRATIONMODE_COOKED;
        }
        else if (prop == DIPROP_ID(DIPROP_FFGAIN))
        {
            value = m_ffGain;
        }
        else if (prop == DIPROP_ID(DIPROP_FFLOAD))
        {
            value = 0;
        }
        else if (prop == DIPROP_ID(DIPROP_AUTOCENTER))
        {
            value = m_autocenter;
        }
        else if (prop == DIPROP_ID(DIPROP_JOYSTICKID))
        {
            value = 0;
        }
        else if (prop == DIPROP_ID(DIPROP_VIDPID))
        {
            value = MAKELONG(G29_VENDOR_ID, G29_PRODUCT_ID);
        }
        else
        {
            return DIERR_UNSUPPORTED;
        }

        if (pdiph->dwSize != sizeof(DIPROPDWORD))
        {
            return DIERR_INVALIDPARAM;
        }

        ((LPDIPROPDWORD)pdiph)->dwData = value;
        return DI_OK;
    }

    HRESULT set_property_locked(ULONG_PTR prop, LPCDIPROPHEADER pdiph)
    {
        const VirtualObject* objects = virtual_objects();
        int obj = -1;

        if (pdiph->dwHow != DIPH_DEVICE)
        {
            obj = find_object_locked(pdiph->dwObj, pdiph->dwHow);

            if (obj < 0)
            {
                return DIERR_OBJECTNOTFOUND;
            }
        }

        // DIPH_DEVICE applies an axis property to every axis.
        int firstAxis = 0;
        int lastAxis = WHEEL_AXIS_COUNT - 1;

        if (obj >= 0)
        {
            if (objects[obj].kind != VOBJ_AXIS)
            {
                firstAxis = 1;
                lastAxis = 0;
            }
            else
            {
                firstAxis = lastAxis = objects[obj].index;
            }
        }

        if (prop == DIPROP_ID(DIPROP_RANGE))
        {
            if (pdiph->dwSize != sizeof(DIPROPRANGE))
            {
                return DIERR_INVALIDPARAM;
            }

            LPCDIPROPRANGE range = (LPCDIPROPRANGE)pdiph;

            if (range->lMin >= range->lMax)
            {
                return DIERR_INVALIDPARAM;
            }

            if (firstAxis > lastAxis)
            {
                return DIERR_UNSUPPORTED;
            }

            for (int a = firstAxis; a <= lastAxis; ++a)
            {
                m_rangeMin[a] = range->lMin;
                m_rangeMax[a] = range->lMax;
            }

            return DI_OK;
        }

        if (prop == DIPROP_ID(DIPROP_APPDATA))
        {
            return DI_OK;
        }

        if (pdiph->dwSize != sizeof(DIPROPDWORD))
        {
            return DIERR_INVALIDPARAM;
        }

        DWORD value = ((LPCDIPROPDWORD)pdiph)->dwData;

        if (prop == DIPROP_ID(DIPROP_DEADZONE) || prop == DIPROP_ID(DIPROP_SATURATION))
        {
            if (value > 10000)
            {
                return DIERR_INVALIDPARAM;
            }

            if (firstAxis > lastAxis)
            {
                return DIERR_UNSUPPORTED;
            }

            for (int a = firstAxis; a <= lastAxis; ++a)
            {
                if (prop == DIPROP_ID(DIPROP_DEADZONE))
                {
                    m_deadzone[a] = value;
                }
                else
                {
                    m_saturation[a] = value;
                }
            }

            return DI_OK;
        }

        if (prop == DIPROP_ID(DIPROP_BUFFERSIZE))
        {
            if (value > VIRTUAL_MAX_BUFFER)
            {
                value = VIRTUAL_MAX_BUFFER;
            }

            delete[] m_buffer;
            m_buffer = value ? new DIDEVICEOBJECTDATA[value] : NULL;
            m_bufferSize = value;
            m_bufferHead = 0;
            m_bufferCount = 0;
            m_overflow = false;
            return DI_OK;
        }

        if (prop == DIPROP_ID(DIPROP_FFGAIN))
        {
            if (value > 10000)
            {
                return DIERR_INVALIDPARAM;
            }

            m_ffGain = value;
            return DI_OK;
        }

        if (prop == DIPROP_ID(DIPROP_AUTOCENTER))
        {
            m_autocenter = value;
            return DI_OK;
        }

        if (prop == DIPROP_ID(DIPROP_AXISMODE) || prop == DIPROP_ID(DIPROP_CALIBRATIONMODE))
        {
            return DI_OK;
        }

        return DIERR_UNSUPPORTED;
    }

public:
    VirtualG29Device()
        : m_refs(1),
          m_acquired(false),
          m_formatSet(false),
          m_dataSize(0),
          m_extraPovCount(0),
          m_buffer(NULL),
          m_bufferSize(0),
          m_bufferHead(0),
          m_bufferCount(0),
          m_overflow(false),
          m_event(NULL),
          m_ffGain(10000),
          m_autocenter(DIPROPAUTOCENTER_ON),
          m_stateReads(0)
    {
        for (int i = 0; i < VIRTUAL_OBJECT_COUNT; ++i)
        {
            m_userOffset[i] = -1;
        }

        for (int a = 0; a < WHEEL_AXIS_COUNT; ++a)
        {
            m_rangeMin[a] = 0;
            m_rangeMax[a] = 65535;
            m_deadzone[a] = 0;
            m_saturation[a] = 10000;
        }

        bool registered = false;

        EnterCriticalSection(&g_inputLock);

        for (int i = 0; i < VIRTUAL_MAX_DEVICES; ++i)
        {
            if (!g_virtualDevices[i])
            {
                g_virtualDevices[i] = this;
                registered = true;
                break;
            }
        }

        LeaveCriticalSection(&g_inputLock);

        log_line("VirtualG29 created device=%p registered=%s", this, yes_no(registered));
    }

    virtual ~VirtualG29Device()
    {
        EnterCriticalSection(&g_inputLock);

        for (int i = 0; i < VIRTUAL_MAX_DEVICES; ++i)
        {
            if (g_virtualDevices[i] == this)
            {
                g_virtualDevices[i] = NULL;
            }
        }

        delete[] m_buffer;
        m_buffer = NULL;

        LeaveCriticalSection(&g_inputLock);

        log_line("VirtualG29 destroyed device=%p", this);
    }

    // Called by the reader thread with g_inputLock held.
    void on_input_changed_locked(const WheelInput& before, const WheelInput& after)
    {
        if (!m_acquired)
        {
            return;
        }

        bool changed = false;

        for (int i = 0; i < VIRTUAL_OBJECT_COUNT; ++i)
        {
            if (m_userOffset[i] < 0 || !object_changed(i, before, after))
            {
                continue;
            }

            changed = true;

            if (m_bufferSize > 0)
            {
                push_event_locked((DWORD)m_userOffset[i], object_data_locked(i, after));
            }
        }

        if (changed && m_event)
        {
            SetEvent(m_event);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)
    {
        if (!ppvObject)
        {
            return E_POINTER;
        }

        // The older wide device interfaces are prefixes of IDirectInputDevice8W.
        if (IsEqualGUID(riid, IID_IUnknown) ||
            IsEqualGUID(riid, IID_IDirectInputDevice8W) ||
            IsEqualGUID(riid, IID_IDirectInputDevice7W) ||
            IsEqualGUID(riid, IID_IDirectInputDevice2W) ||
            IsEqualGUID(riid, IID_IDirectInputDeviceW))
        {
            *ppvObject = static_cast<IDirectInputDevice8W*>(this);
            AddRef();
            return S_OK;
        }

        char iid[64] = {0};
        guid_to_string(riid, iid, sizeof(iid));
        log_line("VirtualG29::QueryInterface unsupported riid=%s", iid);

        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        return (ULONG)InterlockedIncrement(&m_refs);
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        LONG refs = InterlockedDecrement(&m_refs);

        if (refs == 0)
        {
            delete this;
            return 0;
        }

        return (ULONG)refs;
    }

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS lpDIDevCaps)
    {
        if (!lpDIDevCaps)
        {
            return E_POINTER;
        }

        DWORD size = lpDIDevCaps->dwSize;

        if (size != sizeof(DIDEVCAPS) && size != sizeof(DIDEVCAPS_DX3))
        {
            return DIERR_INVALIDPARAM;
        }

        // The flags the Step 12 patch produced on Wine's device.
        DIDEVCAPS caps;
        ZeroMemory(&caps, sizeof(caps));
        caps.dwSize = size;
        caps.dwFlags =
            DIDC_ATTACHED |
            DIDC_EMULATED |
            DIDC_FORCEFEEDBACK |
            DIDC_FFFADE |
            DIDC_FFATTACK |
            DIDC_POSNEGCOEFFICIENTS |
            DIDC_POSNEGSATURATION |
            DIDC_SATURATION;
        caps.dwDevType = G29_DEVICE_TYPE;
        caps.dwAxes = WHEEL_AXIS_COUNT;
        caps.dwButtons = WHEEL_BUTTON_COUNT;
        caps.dwPOVs = 1;
        caps.dwFFSamplePeriod = 1000;
        caps.dwFFMinTimeResolution = 1000;
        caps.dwFFDriverVersion = 1;

        memcpy(lpDIDevCaps, &caps, size);

        log_line(
            "VirtualG29::GetCapabilities flags=0x%08lx axes=%lu buttons=%lu povs=%lu",
            (unsigned long)caps.dwFlags,
            (unsigned long)caps.dwAxes,
            (unsigned long)caps.dwButtons,
            (unsigned long)caps.dwPOVs
        );

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags)
    {
        if (!lpCallback)
        {
            return DIERR_INVALIDPARAM;
        }

        const VirtualObject* objects = virtual_objects();
        DIDEVICEOBJECTINSTANCEW items[VIRTUAL_OBJECT_COUNT];
        int count = 0;

        EnterCriticalSection(&g_inputLock);

        for (int i = 0; i < VIRTUAL_OBJECT_COUNT; ++i)
        {
            if (!object_matches_enum_filter(objects[i], dwFlags))
            {
                continue;
            }

            items[count].dwSize = sizeof(DIDEVICEOBJECTINSTANCEW);
            fill_object_instance(objects[i], reported_offset_locked(i), &items[count]);
            count++;
        }

        LeaveCriticalSection(&g_inputLock);

        log_line("VirtualG29::EnumObjects flags=0x%08lx objects=%d", (unsigned long)dwFlags, count);

        for (int i = 0; i < count; ++i)
        {
            if (lpCallback(&items[i], pvRef) == DIENUM_STOP)
            {
                break;
            }
        }

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID rguidProp, LPDIPROPHEADER pdiph)
    {
        if (!pdiph || pdiph->dwHeaderSize != sizeof(DIPROPHEADER))
        {
            return DIERR_INVALIDPARAM;
        }

        ULONG_PTR prop = DIPROP_ID(rguidProp);

        EnterCriticalSection(&g_inputLock);
        HRESULT hr = get_property_locked(prop, pdiph);
        LeaveCriticalSection(&g_inputLock);

        log_line(
            "VirtualG29::GetProperty prop=%lu obj=0x%lx how=%lu hr=0x%08lx %s",
            (unsigned long)(prop <= 0xFFFF ? prop : 0),
            (unsigned long)pdiph->dwObj,
            (unsigned long)pdiph->dwHow,
            (unsigned long)hr,
            hresult_name(hr)
        );

        return hr;
    }

    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID rguidProp, LPCDIPROPHEADER pdiph)
    {
        if (!pdiph || pdiph->dwHeaderSize != sizeof(DIPROPHEADER))
        {
            return DIERR_INVALIDPARAM;
        }

        ULONG_PTR prop = DIPROP_ID(rguidProp);

        EnterCriticalSection(&g_inputLock);
        HRESULT hr = set_property_locked(prop, pdiph);
        LeaveCriticalSection(&g_inputLock);

        long firstValue = pdiph->dwSize >= sizeof(DIPROPDWORD) ? (long)((LPCDIPROPDWORD)pdiph)->dwData : 0;
        long secondValue = pdiph->dwSize >= sizeof(DIPROPRANGE) ? (long)((LPCDIPROPRANGE)pdiph)->lMax : 0;

        log_line(
            "VirtualG29::SetProperty prop=%lu obj=0x%lx how=%lu value=%ld,%ld hr=0x%08lx %s",
            (unsigned long)(prop <= 0xFFFF ? prop : 0),
            (unsigned long)pdiph->dwObj,
            (unsigned long)pdiph->dwHow,
            firstValue,
            secondValue,
            (unsigned long)hr,
            hresult_name(hr)
        );

        return hr;
    }

    HRESULT STDMETHODCALLTYPE Acquire()
    {
        EnterCriticalSection(&g_inputLock);

        if (!m_formatSet)
        {
            LeaveCriticalSection(&g_inputLock);
            log_line("VirtualG29::Acquire without data format -> DIERR_INVALIDPARAM");
            return DIERR_INVALIDPARAM;
        }

        if (m_acquired)
        {
            LeaveCriticalSection(&g_inputLock);
            return S_FALSE;
        }

        m_acquired = true;
        m_bufferHead = 0;
        m_bufferCount = 0;
        m_overflow = false;
        m_stateReads = 0;

        LeaveCriticalSection(&g_inputLock);

        log_line("VirtualG29::Acquire");

        tcp_send_line(PROXY_HELLO_LINE);
        tcp_send_line("ACQUIRE");

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Unacquire()
    {
        EnterCriticalSection(&g_inputLock);
        bool wasAcquired = m_acquired;
        m_acquired = false;
        LeaveCriticalSection(&g_inputLock);

        if (!wasAcquired)
        {
            return DI_NOEFFECT;
        }

        log_line("VirtualG29::Unacquire");
        tcp_send_line("UNACQUIRE");

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cbData, LPVOID lpvData)
    {
        if (!lpvData)
        {
            return DIERR_INVALIDPARAM;
        }

        EnterCriticalSection(&g_inputLock);

        if (!m_acquired)
        {
            LeaveCriticalSection(&g_inputLock);
            return DIERR_NOTACQUIRED;
        }

        if (cbData != m_dataSize)
        {
            LeaveCriticalSection(&g_inputLock);
            return DIERR_INVALIDPARAM;
        }

        const VirtualObject* objects = virtual_objects();
        BYTE* out = (BYTE*)lpvData;

        memset(out, 0, cbData);

        for (int i = 0; i < VIRTUAL_OBJECT_COUNT; ++i)
        {
            if (m_userOffset[i] < 0)
            {
                continue;
            }

            DWORD value = object_data_locked(i, g_wheelInput);

            if (objects[i].kind == VOBJ_BUTTON)
            {
                out[m_userOffset[i]] = (BYTE)value;
            }
            else
            {
                memcpy(out + m_userOffset[i], &value, sizeof(value));
            }
        }

        // The format may ask for more hats than the wheel has: centred.
        for (DWORD i = 0; i < m_extraPovCount; ++i)
        {
            memset(out + m_extraPovOffsets[i], 0xFF, sizeof(DWORD));
        }

        DWORD reads = ++m_stateReads;
        WheelInput snapshot = g_wheelInput;
        bool live = g_bridgeInputLive;

        LeaveCriticalSection(&g_inputLock);

        if (reads == 1 || (reads % VIRTUAL_STATE_LOG_EVERY) == 0)
        {
            log_line(
                "VirtualG29::GetDeviceState read=%lu size=%lu live=%s x=%ld y=%ld z=%ld rz=%ld hat=%lu buttons=0x%07lx",
                (unsigned long)reads,
                (unsigned long)cbData,
                yes_no(live),
                (long)snapshot.axes[0],
                (long)snapshot.axes[1],
                (long)snapshot.axes[2],
                (long)snapshot.axes[3],
                (unsigned long)snapshot.hat,
                (unsigned long)snapshot.buttons
            );
        }

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags)
    {
        if (!pdwInOut)
        {
            return DIERR_INVALIDPARAM;
        }

        if (cbObjectData != sizeof(DIDEVICEOBJECTDATA) && cbObjectData != sizeof(DIDEVICEOBJECTDATA_DX3))
        {
            return DIERR_INVALIDPARAM;
        }

        EnterCriticalSection(&g_inputLock);

        if (!m_acquired)
        {
            LeaveCriticalSection(&g_inputLock);
            return DIERR_NOTACQUIRED;
        }

        if (m_bufferSize == 0)
        {
            LeaveCriticalSection(&g_inputLock);
            return DIERR_NOTBUFFERED;
        }

        DWORD count = *pdwInOut < m_bufferCount ? *pdwInOut : m_bufferCount;

        if (rgdod)
        {
            for (DWORD i = 0; i < count; ++i)
            {
                memcpy(
                    (BYTE*)rgdod + i * cbObjectData,
                    &m_buffer[(m_bufferHead + i) % m_bufferSize],
                    cbObjectData
                );
            }
        }

        HRESULT hr = m_overflow ? DI_BUFFEROVERFLOW : DI_OK;

        if (!(dwFlags & DIGDD_PEEK))
        {
            m_bufferHead = (m_bufferHead + count) % m_bufferSize;
            m_bufferCount -= count;
            m_overflow = false;
        }

        LeaveCriticalSection(&g_inputLock);

        *pdwInOut = count;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT lpdf)
    {
        if (!lpdf ||
            lpdf->dwSize != sizeof(DIDATAFORMAT) ||
            lpdf->dwObjSize != sizeof(DIOBJECTDATAFORMAT) ||
            (lpdf->dwNumObjs > 0 && !lpdf->rgodf))
        {
            return DIERR_INVALIDPARAM;
        }

        const VirtualObject* objects = virtual_objects();
        int mapped = 0;
        int unmatched = 0;

        EnterCriticalSection(&g_inputLock);

        if (m_acquired)
        {
            LeaveCriticalSection(&g_inputLock);
            return DIERR_ACQUIRED;
        }

        bool used[VIRTUAL_OBJECT_COUNT] = {false};

        for (int i = 0; i < VIRTUAL_OBJECT_COUNT; ++i)
        {
            m_userOffset[i] = -1;
        }

        m_extraPovCount = 0;

        for (DWORD f = 0; f < lpdf->dwNumObjs; ++f)
        {
            const DIOBJECTDATAFORMAT& od = lpdf->rgodf[f];
            int match = -1;

            for (int i = 0; i < VIRTUAL_OBJECT_COUNT; ++i)
            {
                if (!used[i] && object_matches_format(objects[i], od))
                {
                    match = i;
                    break;
                }
            }

            bool isButton = match >= 0
                ? objects[match].kind == VOBJ_BUTTON
                : (DIDFT_GETTYPE(od.dwType) & DIDFT_BUTTON) != 0;
            DWORD size = isButton ? 1 : sizeof(DWORD);

            if (od.dwOfs + size > lpdf->dwDataSize)
            {
                unmatched++;
                continue;
            }

            if (match >= 0)
            {
                used[match] = true;
                m_userOffset[match] = (LONG)od.dwOfs;
                mapped++;
            }
            else
            {
                unmatched++;

                bool isPov = (od.pguid && IsEqualGUID(*od.pguid, GUID_POV)) ||
                    (DIDFT_GETTYPE(od.dwType) & DIDFT_POV) != 0;

                if (isPov && m_extraPovCount < VIRTUAL_MAX_EXTRA_POVS)
                {
                    m_extraPovOffsets[m_extraPovCount++] = od.dwOfs;
                }
            }
        }

        m_dataSize = lpdf->dwDataSize;
        m_formatSet = true;

        LeaveCriticalSection(&g_inputLock);

        log_line(
            "VirtualG29::SetDataFormat dataSize=%lu objects=%lu mapped=%d unmatched=%d flags=0x%08lx",
            (unsigned long)lpdf->dwDataSize,
            (unsigned long)lpdf->dwNumObjs,
            mapped,
            unmatched,
            (unsigned long)lpdf->dwFlags
        );

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE hEvent)
    {
        EnterCriticalSection(&g_inputLock);

        if (m_acquired)
        {
            LeaveCriticalSection(&g_inputLock);
            return DIERR_ACQUIRED;
        }

        m_event = hEvent;

        LeaveCriticalSection(&g_inputLock);

        log_line("VirtualG29::SetEventNotification event=%p", hEvent);
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hwnd, DWORD dwFlags)
    {
        log_line("VirtualG29::SetCooperativeLevel hwnd=%p flags=0x%08lx", hwnd, (unsigned long)dwFlags);
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW pdidoi, DWORD dwObj, DWORD dwHow)
    {
        if (!pdidoi || dwHow == DIPH_DEVICE)
        {
            return DIERR_INVALIDPARAM;
        }

        EnterCriticalSection(&g_inputLock);
        int i = find_object_locked(dwObj, dwHow);
        DWORD ofs = i >= 0 ? reported_offset_locked(i) : 0;
        LeaveCriticalSection(&g_inputLock);

        if (i < 0)
        {
            log_line("VirtualG29::GetObjectInfo obj=0x%lx how=%lu not found", (unsigned long)dwObj, (unsigned long)dwHow);
            return DIERR_OBJECTNOTFOUND;
        }

        return fill_object_instance(virtual_objects()[i], ofs, pdidoi);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW pdidi)
    {
        if (!pdidi)
        {
            return E_POINTER;
        }

        return fill_virtual_instance(pdidi);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags)
    {
        (void)hwndOwner;
        (void)dwFlags;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion, REFGUID rguid)
    {
        (void)hinst;
        (void)dwVersion;
        (void)rguid;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID rguid, LPCDIEFFECT lpeff, LPDIRECTINPUTEFFECT* ppdeff, LPUNKNOWN punkOuter)
    {
        (void)punkOuter;

        log_line("VirtualG29::CreateEffect name=%s", effect_guid_to_string(rguid));
        return g29_create_effect(rguid, lpeff, ppdeff);
    }

    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW lpCallback, LPVOID pvRef, DWORD dwEffType)
    {
        log_line("VirtualG29::EnumEffects effType=0x%08lx", (unsigned long)dwEffType);
        return g29_enum_effects(lpCallback, pvRef);
    }

    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW pdei, REFGUID rguid)
    {
        return g29_get_effect_info(pdei, rguid);
    }

    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD pdwOut)
    {
        if (pdwOut)
        {
            *pdwOut = 0;
        }

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD dwFlags)
    {
        log_line("VirtualG29::SendForceFeedbackCommand flags=0x%08lx", (unsigned long)dwFlags);
        return g29_send_force_feedback_command(dwFlags);
    }

    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK lpCallback, LPVOID pvRef, DWORD fl)
    {
        (void)lpCallback;
        (void)pvRef;
        (void)fl;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE pesc)
    {
        (void)pesc;
        log_line("VirtualG29::Escape -> DIERR_UNSUPPORTED");
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE Poll()
    {
        EnterCriticalSection(&g_inputLock);
        bool acquired = m_acquired;
        LeaveCriticalSection(&g_inputLock);

        return acquired ? DI_OK : DIERR_NOTACQUIRED;
    }

    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD cbObjectData, LPCDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD fl)
    {
        (void)cbObjectData;
        (void)rgdod;
        (void)pdwInOut;
        (void)fl;
        log_line("VirtualG29::SendDeviceData -> DIERR_UNSUPPORTED");
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR lpszFileName, LPDIENUMEFFECTSINFILECALLBACK pec, LPVOID pvRef, DWORD dwFlags)
    {
        (void)lpszFileName;
        (void)pec;
        (void)pvRef;
        (void)dwFlags;
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR lpszFileName, DWORD dwEntries, LPDIFILEEFFECT rgDiFileEft, DWORD dwFlags)
    {
        (void)lpszFileName;
        (void)dwEntries;
        (void)rgDiFileEft;
        (void)dwFlags;
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATW lpdiaf, LPCWSTR lpszUserName, DWORD dwFlags)
    {
        (void)lpdiaf;
        (void)lpszUserName;
        log_line("VirtualG29::BuildActionMap flags=0x%08lx -> DIERR_UNSUPPORTED", (unsigned long)dwFlags);
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATW lpdiActionFormat, LPCWSTR lptszUserName, DWORD dwFlags)
    {
        (void)lpdiActionFormat;
        (void)lptszUserName;
        log_line("VirtualG29::SetActionMap flags=0x%08lx -> DIERR_UNSUPPORTED", (unsigned long)dwFlags);
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERW lpdiDevImageInfoHeader)
    {
        (void)lpdiDevImageInfoHeader;
        return DIERR_UNSUPPORTED;
    }
};

static void virtual_devices_input_changed_locked(const WheelInput& before, const WheelInput& after)
{
    for (int i = 0; i < VIRTUAL_MAX_DEVICES; ++i)
    {
        if (g_virtualDevices[i])
        {
            g_virtualDevices[i]->on_input_changed_locked(before, after);
        }
    }
}

// Whether an EnumDevices request covers game controllers, and so the wheel.
static bool enum_class_includes_wheel(DWORD dwDevType)
{
    return
        dwDevType == DI8DEVCLASS_ALL ||
        dwDevType == DI8DEVCLASS_GAMECTRL ||
        GET_DIDEVICE_TYPE(dwDevType) == DI8DEVTYPE_DRIVING;
}

struct EnumDevicesWContext
{
    LPDIENUMDEVICESCALLBACKW originalCallback;
    LPVOID originalRef;
    DWORD requestedClass;
    DWORD requestedFlags;
    bool sawG29;
    bool wineGuidTaken;
    bool stopped;
};

static BOOL CALLBACK enum_devices_w_logging_callback(const DIDEVICEINSTANCEW* instance, VOID* ref)
{
    EnumDevicesWContext* ctx = (EnumDevicesWContext*)ref;

    if (!instance || !ctx)
    {
        return DIENUM_CONTINUE;
    }

    char guidInstance[64] = {0};
    char guidProduct[64] = {0};
    char instanceName[512] = {0};
    char productName[512] = {0};

    guid_to_string(instance->guidInstance, guidInstance, sizeof(guidInstance));
    guid_to_string(instance->guidProduct, guidProduct, sizeof(guidProduct));
    wide_to_utf8(instance->tszInstanceName, instanceName, sizeof(instanceName));
    wide_to_utf8(instance->tszProductName, productName, sizeof(productName));

    bool isG29 = instance_is_g29_w(instance);
    bool forceFeedbackRequested = (ctx->requestedFlags & DIEDFL_FORCEFEEDBACK) != 0;

    if (isG29)
    {
        ctx->sawG29 = true;
    }
    else if (IsEqualGUID(instance->guidInstance, kWineG29InstanceGuid))
    {
        ctx->wineGuidTaken = true;
    }

    log_line(
        "DirectInput8WProxy::EnumDevices item class=0x%08lx className=%s flags=0x%08lx forceFeedbackRequested=%s type=0x%08lx typeName=%s instance='%s' product='%s' guidInstance=%s guidProduct=%s isG29=%s",
        (unsigned long)ctx->requestedClass,
        devclass_to_string(ctx->requestedClass),
        (unsigned long)ctx->requestedFlags,
        yes_no(forceFeedbackRequested),
        (unsigned long)instance->dwDevType,
        device_type_to_string(instance->dwDevType),
        instanceName,
        productName,
        guidInstance,
        guidProduct,
        yes_no(isG29)
    );

    if (forceFeedbackRequested && !isG29)
    {
        log_line("DirectInput8WProxy::EnumDevices skipping non-G29 during FORCEFEEDBACK request");
        return DIENUM_CONTINUE;
    }

    if (forceFeedbackRequested && isG29)
    {
        log_line("DirectInput8WProxy::EnumDevices passing G29 during FORCEFEEDBACK request");
    }

    if (ctx->originalCallback)
    {
        if (isG29)
        {
            /*
                Step 13:
                Pass a patched copy of DIDEVICEINSTANCEW to ETS2.
                Wine exposes the G29 as input-only and guidFFDriver is likely GUID_NULL.
                Some games use guidFFDriver as an extra hint for Force Feedback support.
            */
            DIDEVICEINSTANCEW patchedInstance;
            ZeroMemory(&patchedInstance, sizeof(patchedInstance));
            patchedInstance = *instance;

            char oldFFGuid[64] = {0};
            char newFFGuid[64] = {0};

            guid_to_string(patchedInstance.guidFFDriver, oldFFGuid, sizeof(oldFFGuid));

            patchedInstance.guidFFDriver = kG29FFDriverGuid;

            guid_to_string(patchedInstance.guidFFDriver, newFFGuid, sizeof(newFFGuid));

            log_line(
                "DirectInput8WProxy::EnumDevices Step13 patched G29 guidFFDriver old=%s new=%s",
                oldFFGuid,
                newFFGuid
            );

            BOOL result = ctx->originalCallback(&patchedInstance, ctx->originalRef);
            ctx->stopped = result == DIENUM_STOP;
            return result;
        }

        BOOL result = ctx->originalCallback(instance, ctx->originalRef);
        ctx->stopped = result == DIENUM_STOP;
        return result;
    }

    return DIENUM_CONTINUE;
}

class DirectInput8WProxy : public IDirectInput8W
{
private:
    IDirectInput8W* m_real;
    LONG m_refs;

public:
    DirectInput8WProxy(IDirectInput8W* real)
        : m_real(real), m_refs(1)
    {
        log_line("DirectInput8WProxy created real=%p", m_real);
    }

    virtual ~DirectInput8WProxy()
    {
        log_line("DirectInput8WProxy destroyed");
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)
    {
        char iid[64] = {0};
        guid_to_string(riid, iid, sizeof(iid));

        log_line("DirectInput8WProxy::QueryInterface riid=%s", iid);

        if (!ppvObject)
        {
            return E_POINTER;
        }

        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IDirectInput8W))
        {
            *ppvObject = static_cast<IDirectInput8W*>(this);
            AddRef();
            return S_OK;
        }

        return m_real->QueryInterface(riid, ppvObject);
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        LONG refs = InterlockedIncrement(&m_refs);
        log_line("DirectInput8WProxy::AddRef refs=%ld", refs);
        return (ULONG)refs;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        LONG refs = InterlockedDecrement(&m_refs);
        log_line("DirectInput8WProxy::Release refs=%ld", refs);

        if (refs == 0)
        {
            ULONG realRefs = m_real->Release();
            log_line("DirectInput8WProxy::Release released real refs=%lu", realRefs);
            delete this;
            return 0;
        }

        return (ULONG)refs;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID rguid, LPDIRECTINPUTDEVICE8W* lplpDirectInputDevice, LPUNKNOWN pUnkOuter)
    {
        char guidText[64] = {0};
        guid_to_string(rguid, guidText, sizeof(guidText));

        log_line(
            "DirectInput8WProxy::CreateDevice begin guid=%s out=%p outer=%p",
            guidText,
            lplpDirectInputDevice,
            pUnkOuter
        );

        if (g_virtualOffered && IsEqualGUID(rguid, g_virtualInstanceGuid))
        {
            if (!lplpDirectInputDevice)
            {
                return E_POINTER;
            }

            *lplpDirectInputDevice = new VirtualG29Device();

            log_line("DirectInput8WProxy::CreateDevice returning virtual G29=%p", *lplpDirectInputDevice);
            return DI_OK;
        }

        IDirectInputDevice8W* realDevice = NULL;

        HRESULT hr = m_real->CreateDevice(rguid, &realDevice, pUnkOuter);

        log_line(
            "DirectInput8WProxy::CreateDevice real hr=0x%08lx %s realDevice=%p",
            (unsigned long)hr,
            hresult_name(hr),
            realDevice
        );

        if (FAILED(hr) || !realDevice || !lplpDirectInputDevice)
        {
            if (lplpDirectInputDevice)
            {
                *lplpDirectInputDevice = realDevice;
            }

            return hr;
        }

        bool isG29 = false;

        DIDEVICEINSTANCEW info;
        ZeroMemory(&info, sizeof(info));
        info.dwSize = sizeof(info);

        HRESULT hrInfo = realDevice->GetDeviceInfo(&info);

        if (SUCCEEDED(hrInfo))
        {
            char instanceName[512] = {0};
            char productName[512] = {0};

            wide_to_utf8(info.tszInstanceName, instanceName, sizeof(instanceName));
            wide_to_utf8(info.tszProductName, productName, sizeof(productName));

            isG29 = instance_is_g29_w(&info);

            log_line(
                "DirectInput8WProxy::CreateDevice deviceInfo hr=0x%08lx instance='%s' product='%s' type=0x%08lx typeName=%s isG29=%s",
                (unsigned long)hrInfo,
                instanceName,
                productName,
                (unsigned long)info.dwDevType,
                device_type_to_string(info.dwDevType),
                yes_no(isG29)
            );
        }
        else
        {
            log_line("DirectInput8WProxy::CreateDevice GetDeviceInfo hr=0x%08lx %s", (unsigned long)hrInfo, hresult_name(hrInfo));
        }

        if (isG29)
        {
            DirectInputDevice8WProxy* proxyDevice = new DirectInputDevice8WProxy(realDevice, true);

            /*
                Step 12:
                Safe internal capability probe.
                We call our own proxy GetCapabilities once, before returning the proxy to ETS2.
                This does not call any game callback and should not affect enumeration flow.
            */
            DIDEVCAPS probeCaps;
            ZeroMemory(&probeCaps, sizeof(probeCaps));
            probeCaps.dwSize = sizeof(probeCaps);

            HRESULT hrProbeCaps = proxyDevice->GetCapabilities(&probeCaps);

            log_line(
                "Step12 internal proxy GetCapabilities hr=0x%08lx %s flags=0x%08lx FORCEFEEDBACK=%s axes=%lu buttons=%lu povs=%lu ffSamplePeriod=%lu ffMinTimeResolution=%lu ffDriver=%lu",
                (unsigned long)hrProbeCaps,
                hresult_name(hrProbeCaps),
                (unsigned long)probeCaps.dwFlags,
                yes_no((probeCaps.dwFlags & DIDC_FORCEFEEDBACK) != 0),
                (unsigned long)probeCaps.dwAxes,
                (unsigned long)probeCaps.dwButtons,
                (unsigned long)probeCaps.dwPOVs,
                (unsigned long)probeCaps.dwFFSamplePeriod,
                (unsigned long)probeCaps.dwFFMinTimeResolution,
                (unsigned long)probeCaps.dwFFDriverVersion
            );

            *lplpDirectInputDevice = static_cast<IDirectInputDevice8W*>(proxyDevice);

            log_line("DirectInput8WProxy::CreateDevice returning G29 DeviceProxy=%p", *lplpDirectInputDevice);
            return hr;
        }

        *lplpDirectInputDevice = realDevice;

        log_line("DirectInput8WProxy::CreateDevice returning real device without proxy");
        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD dwDevType, LPDIENUMDEVICESCALLBACKW lpCallback, LPVOID pvRef, DWORD dwFlags)
    {
        bool forceFeedbackRequested = (dwFlags & DIEDFL_FORCEFEEDBACK) != 0;
        DWORD wineFlags = dwFlags;

        if (forceFeedbackRequested)
        {
            wineFlags = dwFlags & ~DIEDFL_FORCEFEEDBACK;
        }

        log_line(
            "DirectInput8WProxy::EnumDevices begin class=0x%08lx className=%s originalFlags=0x%08lx wineFlags=0x%08lx forceFeedbackRequested=%s callback=%p ref=%p",
            (unsigned long)dwDevType,
            devclass_to_string(dwDevType),
            (unsigned long)dwFlags,
            (unsigned long)wineFlags,
            yes_no(forceFeedbackRequested),
            lpCallback,
            pvRef
        );

        EnumDevicesWContext ctx;
        ZeroMemory(&ctx, sizeof(ctx));
        ctx.originalCallback = lpCallback;
        ctx.originalRef = pvRef;
        ctx.requestedClass = dwDevType;
        ctx.requestedFlags = dwFlags;

        HRESULT hr = m_real->EnumDevices(
            dwDevType,
            enum_devices_w_logging_callback,
            &ctx,
            wineFlags
        );

        log_line(
            "DirectInput8WProxy::EnumDevices end hr=0x%08lx %s originalFlags=0x%08lx wineFlags=0x%08lx",
            (unsigned long)hr,
            hresult_name(hr),
            (unsigned long)dwFlags,
            (unsigned long)wineFlags
        );

        if (ctx.sawG29 && !g_realG29Seen)
        {
            g_realG29Seen = true;
            log_line("DirectInput8WProxy::EnumDevices real G29 present, virtual disabled");
        }

        if (SUCCEEDED(hr) && lpCallback && !ctx.stopped && !g_realG29Seen && enum_class_includes_wheel(dwDevType))
        {
            offer_virtual_g29(lpCallback, pvRef, ctx.wineGuidTaken);
        }

        return hr;
    }

    // Wine enumerated no G29: add ours, provided the bridge can feed it.
    static void offer_virtual_g29(LPDIENUMDEVICESCALLBACKW lpCallback, LPVOID pvRef, bool wineGuidTaken)
    {
        if (!g_virtualGuidChosen)
        {
            g_virtualInstanceGuid = wineGuidTaken ? kCrossFFBInstanceGuid : kWineG29InstanceGuid;
            g_virtualGuidChosen = true;
        }

        if (!bridge_input_start())
        {
            log_line("DirectInput8WProxy::EnumDevices no G29 from Wine and bridge not reachable, virtual G29 not offered");
            return;
        }

        DIDEVICEINSTANCEW instance;
        ZeroMemory(&instance, sizeof(instance));
        instance.dwSize = sizeof(instance);
        fill_virtual_instance(&instance);

        g_virtualOffered = true;

        char guidText[64] = {0};
        guid_to_string(instance.guidInstance, guidText, sizeof(guidText));

        log_line("DirectInput8WProxy::EnumDevices offering virtual G29 guidInstance=%s", guidText);

        lpCallback(&instance, pvRef);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID rguidInstance)
    {
        char guidText[64] = {0};
        guid_to_string(rguidInstance, guidText, sizeof(guidText));

        // Reported attached even while the bridge reconnects: the reader
        // keeps retrying and the state stays neutral in the meantime.
        if (g_virtualOffered && IsEqualGUID(rguidInstance, g_virtualInstanceGuid))
        {
            log_line("DirectInput8WProxy::GetDeviceStatus virtual G29 guid=%s -> DI_OK", guidText);
            return DI_OK;
        }

        HRESULT hr = m_real->GetDeviceStatus(rguidInstance);

        log_line(
            "DirectInput8WProxy::GetDeviceStatus guid=%s hr=0x%08lx %s",
            guidText,
            (unsigned long)hr,
            hresult_name(hr)
        );

        return hr;
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags)
    {
        return m_real->RunControlPanel(hwndOwner, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion)
    {
        return m_real->Initialize(hinst, dwVersion);
    }

    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID rguidClass, LPCWSTR ptszName, LPGUID pguidInstance)
    {
        char guidClass[64] = {0};
        char name[512] = {0};

        guid_to_string(rguidClass, guidClass, sizeof(guidClass));
        wide_to_utf8(ptszName, name, sizeof(name));

        log_line(
            "DirectInput8WProxy::FindDevice class=%s name='%s' out=%p",
            guidClass,
            name,
            pguidInstance
        );

        HRESULT hr = m_real->FindDevice(rguidClass, ptszName, pguidInstance);

        log_line("DirectInput8WProxy::FindDevice hr=0x%08lx %s", (unsigned long)hr, hresult_name(hr));

        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(
        LPCWSTR ptszUserName,
        LPDIACTIONFORMATW lpdiActionFormat,
        LPDIENUMDEVICESBYSEMANTICSCBW lpCallback,
        LPVOID pvRef,
        DWORD dwFlags)
    {
        char userName[512] = {0};
        wide_to_utf8(ptszUserName, userName, sizeof(userName));

        log_line(
            "DirectInput8WProxy::EnumDevicesBySemantics user='%s' actionFormat=%p callback=%p ref=%p flags=0x%08lx",
            userName,
            lpdiActionFormat,
            lpCallback,
            pvRef,
            (unsigned long)dwFlags
        );

        HRESULT hr = m_real->EnumDevicesBySemantics(
            ptszUserName,
            lpdiActionFormat,
            lpCallback,
            pvRef,
            dwFlags
        );

        log_line(
            "DirectInput8WProxy::EnumDevicesBySemantics hr=0x%08lx %s",
            (unsigned long)hr,
            hresult_name(hr)
        );

        return hr;
    }

    HRESULT STDMETHODCALLTYPE ConfigureDevices(
        LPDICONFIGUREDEVICESCALLBACK lpdiCallback,
        LPDICONFIGUREDEVICESPARAMSW lpdiCDParams,
        DWORD dwFlags,
        LPVOID pvRefData)
    {
        log_line(
            "DirectInput8WProxy::ConfigureDevices callback=%p params=%p flags=0x%08lx ref=%p",
            lpdiCallback,
            lpdiCDParams,
            (unsigned long)dwFlags,
            pvRefData
        );

        HRESULT hr = m_real->ConfigureDevices(
            lpdiCallback,
            lpdiCDParams,
            dwFlags,
            pvRefData
        );

        log_line(
            "DirectInput8WProxy::ConfigureDevices hr=0x%08lx %s",
            (unsigned long)hr,
            hresult_name(hr)
        );

        return hr;
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            InitializeCriticalSection(&g_tcpLock);
            InitializeCriticalSection(&g_inputLock);
            log_line("DllMain PROCESS_ATTACH - proxy step18 tcp bridge loaded");
            break;

        case DLL_PROCESS_DETACH:
            log_line("DllMain PROCESS_DETACH - proxy step18 tcp unloaded");

            // At process exit other threads are already gone, possibly while
            // holding g_tcpLock, and the system closes the socket anyway.
            if (reserved == NULL)
            {
                tcp_close();
            }
            break;
    }

    return TRUE;
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD dwVersion,
    REFIID riidltf,
    LPVOID* ppvOut,
    LPUNKNOWN punkOuter)
{
    char riidText[64] = {0};
    guid_to_string(riidltf, riidText, sizeof(riidText));

    log_line(
        "DirectInput8Create called: hinst=%p dwVersion=0x%08lx riid=%s ppvOut=%p punkOuter=%p",
        hinst,
        (unsigned long)dwVersion,
        riidText,
        ppvOut,
        punkOuter
    );

    if (!load_real_dinput8())
    {
        log_line("Cannot load real dinput8, returning E_FAIL");

        if (ppvOut)
        {
            *ppvOut = NULL;
        }

        return E_FAIL;
    }

    LPVOID realObject = NULL;

    HRESULT hr = g_realDirectInput8Create(
        hinst,
        dwVersion,
        riidltf,
        &realObject,
        punkOuter
    );

    log_line(
        "Real DirectInput8Create returned hr=0x%08lx %s realObject=%p",
        (unsigned long)hr,
        hresult_name(hr),
        realObject
    );

    if (FAILED(hr) || !realObject || !ppvOut)
    {
        if (ppvOut)
        {
            *ppvOut = realObject;
        }

        return hr;
    }

    if (IsEqualGUID(riidltf, IID_IDirectInput8W))
    {
        DirectInput8WProxy* proxy = new DirectInput8WProxy((IDirectInput8W*)realObject);
        *ppvOut = static_cast<IDirectInput8W*>(proxy);

        log_line("DirectInput8Create returning IDirectInput8W proxy=%p", *ppvOut);
        return hr;
    }

    *ppvOut = realObject;

    log_line("DirectInput8Create returning real object without wrapper");
    return hr;
}
