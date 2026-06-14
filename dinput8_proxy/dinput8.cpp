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

static void log_line(const char* fmt, ...)
{
    char exePath[MAX_PATH] = {0};
    char logPath[MAX_PATH] = {0};

    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);

    if (len > 0)
    {
        lstrcpyA(logPath, exePath);

        char* lastSlash = NULL;
        for (char* p = logPath; *p; ++p)
        {
            if (*p == '\\' || *p == '/')
            {
                lastSlash = p;
            }
        }

        if (lastSlash)
        {
            *(lastSlash + 1) = '\0';
            lstrcatA(logPath, "dinput8_proxy.log");
        }
        else
        {
            lstrcpyA(logPath, "dinput8_proxy.log");
        }
    }
    else
    {
        lstrcpyA(logPath, "dinput8_proxy.log");
    }

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

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\r\n");
    fclose(f);
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


static SOCKET g_tcpSocket = INVALID_SOCKET;
static bool g_wsaStarted = false;
static bool g_tcpConnectAttempted = false;

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

    log_line("TCP connecting to 127.0.0.1:54321");

    int rc = connect(s, (sockaddr*)&addr, sizeof(addr));

    if (rc == SOCKET_ERROR)
    {
        log_line("TCP connect failed error=%d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    g_tcpSocket = s;

    log_line("TCP connected to 127.0.0.1:54321");
    return true;
}

static void tcp_close()
{
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
}

static void tcp_send_line(const char* fmt, ...)
{
    if (!tcp_connect_once())
    {
        return;
    }

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

    int len = (int)strlen(line);
    int sent = send(g_tcpSocket, line, len, 0);

    if (sent == SOCKET_ERROR)
    {
        log_line("TCP send failed error=%d", WSAGetLastError());
        closesocket(g_tcpSocket);
        g_tcpSocket = INVALID_SOCKET;
        g_tcpConnectAttempted = false;
        return;
    }

    log_line("TCP sent: %s", line);
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

        log_line(
            "FakeEffect created guid=%s name=%s",
            guidText,
            effect_guid_to_string(guid)
        );
    }

    virtual ~FakeDirectInputEffect()
    {
        log_line("FakeEffect destroyed");
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)
    {
        char iid[64] = {0};
        guid_to_string(riid, iid, sizeof(iid));

        log_line("FakeEffect::QueryInterface riid=%s", iid);

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
        log_line("FakeEffect::AddRef refs=%ld", refs);
        return (ULONG)refs;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        LONG refs = InterlockedDecrement(&m_refs);
        log_line("FakeEffect::Release refs=%ld", refs);

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

        log_line(
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
        log_line("FakeEffect::GetEffectGuid out=%p", pguid);

        if (!pguid)
        {
            return DIERR_INVALIDPARAM;
        }

        *pguid = m_guid;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetParameters(LPDIEFFECT peff, DWORD dwFlags)
    {
        log_line("FakeEffect::GetParameters effect=%p flags=0x%08lx", peff, (unsigned long)dwFlags);
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SetParameters(LPCDIEFFECT peff, DWORD dwFlags)
    {
        log_line("FakeEffect::SetParameters effect=%p flags=0x%08lx", peff, (unsigned long)dwFlags);

        if (peff)
        {
            log_line(
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
                    log_line(
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
                    log_line(
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

                log_line(
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

                log_line(
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

        log_line(
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
        log_line("FakeEffect::Stop");
        tcp_send_line("STOP");
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEffectStatus(LPDWORD pdwFlags)
    {
        log_line("FakeEffect::GetEffectStatus out=%p started=%s", pdwFlags, yes_no(m_started));

        if (pdwFlags)
        {
            *pdwFlags = m_started ? DIEGES_PLAYING : 0;
        }

        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Download()
    {
        log_line("FakeEffect::Download");
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Unload()
    {
        log_line("FakeEffect::Unload");
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE pesc)
    {
        log_line("FakeEffect::Escape escape=%p", pesc);
        return DI_OK;
    }
};


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
            tcp_send_line("HELLO source=dinput8_proxy step17");
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

                log_line("DeviceProxy::CreateEffect returning fake effect=%p", *ppdeff);

                tcp_send_line(
                    "CREATE_EFFECT name=%s",
                    effect_guid_to_string(rguid)
                );

                return DI_OK;
            }

            log_line("DeviceProxy::CreateEffect unsupported fake guid -> DIERR_UNSUPPORTED");
            return DIERR_UNSUPPORTED;
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

        /*
            Step 11 clean:
            For the G29 we do NOT call Wine's real EnumEffects anymore.
            Wine reports no real FFB effects for this device.
            We expose only our fake DirectInput effects to ETS2.
        */
        log_line("DeviceProxy::EnumEffects G29 clean fake-only mode active");

        if (!lpCallback)
        {
            log_line("DeviceProxy::EnumEffects no callback, returning DI_OK");
            return DI_OK;
        }

        DIEFFECTINFOW info;
        ZeroMemory(&info, sizeof(info));
        info.dwSize = sizeof(info);

        info.guid = GUID_ConstantForce;
        info.dwEffType = DIEFT_CONSTANTFORCE;
        info.dwStaticParams = DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_DURATION | DIEP_GAIN | DIEP_AXES;
        info.dwDynamicParams = DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_GAIN;
        lstrcpynW(info.tszName, L"Constant Force", MAX_PATH);

        log_line("DeviceProxy::EnumEffects emitting fake GUID_ConstantForce");

        if (lpCallback(&info, pvRef) == DIENUM_STOP)
        {
            return DI_OK;
        }

        ZeroMemory(&info, sizeof(info));
        info.dwSize = sizeof(info);
        info.guid = GUID_Spring;
        info.dwEffType = DIEFT_CONDITION;
        info.dwStaticParams = DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_DURATION | DIEP_GAIN | DIEP_AXES;
        info.dwDynamicParams = DIEP_TYPESPECIFICPARAMS | DIEP_GAIN;
        lstrcpynW(info.tszName, L"Spring", MAX_PATH);

        log_line("DeviceProxy::EnumEffects emitting fake GUID_Spring");

        if (lpCallback(&info, pvRef) == DIENUM_STOP)
        {
            return DI_OK;
        }

        ZeroMemory(&info, sizeof(info));
        info.dwSize = sizeof(info);
        info.guid = GUID_Damper;
        info.dwEffType = DIEFT_CONDITION;
        info.dwStaticParams = DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_DURATION | DIEP_GAIN | DIEP_AXES;
        info.dwDynamicParams = DIEP_TYPESPECIFICPARAMS | DIEP_GAIN;
        lstrcpynW(info.tszName, L"Damper", MAX_PATH);

        log_line("DeviceProxy::EnumEffects emitting fake GUID_Damper");

        if (lpCallback(&info, pvRef) == DIENUM_STOP)
        {
            return DI_OK;
        }

        ZeroMemory(&info, sizeof(info));
        info.dwSize = sizeof(info);
        info.guid = GUID_Friction;
        info.dwEffType = DIEFT_CONDITION;
        info.dwStaticParams = DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_DURATION | DIEP_GAIN | DIEP_AXES;
        info.dwDynamicParams = DIEP_TYPESPECIFICPARAMS | DIEP_GAIN;
        lstrcpynW(info.tszName, L"Friction", MAX_PATH);

        log_line("DeviceProxy::EnumEffects emitting fake GUID_Friction");

        if (lpCallback(&info, pvRef) == DIENUM_STOP)
        {
            return DI_OK;
        }

        return DI_OK;
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
            tcp_send_line(
                "FF_COMMAND flags=0x%08lx",
                (unsigned long)dwFlags
            );

            return DI_OK;
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

struct EnumDevicesWContext
{
    LPDIENUMDEVICESCALLBACKW originalCallback;
    LPVOID originalRef;
    DWORD requestedClass;
    DWORD requestedFlags;
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

            /*
                We use a stable fake non-null GUID.
                It does not need to correspond to a real Windows FF driver,
                because CreateEffect is handled by our DeviceProxy.
            */
            patchedInstance.guidFFDriver.Data1 = 0xC24F046D;
            patchedInstance.guidFFDriver.Data2 = 0xF00D;
            patchedInstance.guidFFDriver.Data3 = 0x0001;
            patchedInstance.guidFFDriver.Data4[0] = 0x90;
            patchedInstance.guidFFDriver.Data4[1] = 0x29;
            patchedInstance.guidFFDriver.Data4[2] = 0x47;
            patchedInstance.guidFFDriver.Data4[3] = 0x32;
            patchedInstance.guidFFDriver.Data4[4] = 0x39;
            patchedInstance.guidFFDriver.Data4[5] = 0x46;
            patchedInstance.guidFFDriver.Data4[6] = 0x46;
            patchedInstance.guidFFDriver.Data4[7] = 0x42;

            guid_to_string(patchedInstance.guidFFDriver, newFFGuid, sizeof(newFFGuid));

            log_line(
                "DirectInput8WProxy::EnumDevices Step13 patched G29 guidFFDriver old=%s new=%s",
                oldFFGuid,
                newFFGuid
            );

            return ctx->originalCallback(&patchedInstance, ctx->originalRef);
        }

        return ctx->originalCallback(instance, ctx->originalRef);
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

        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID rguidInstance)
    {
        char guidText[64] = {0};
        guid_to_string(rguidInstance, guidText, sizeof(guidText));

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
    (void)reserved;

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            log_line("DllMain PROCESS_ATTACH - proxy step17 tcp bridge loaded");
            break;

        case DLL_PROCESS_DETACH:
            log_line("DllMain PROCESS_DETACH - proxy step17 tcp unloaded");
            tcp_close();
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
