// Console probe for the G29 as a game sees it through DirectInput 8.
//
// Run it inside a bottle with the proxy dinput8.dll next to it. It lists the
// game controllers, opens the G29, prints its capabilities and objects, then
// reads it the way games do: c_dfDIJoystick2 with buffered data, followed by
// a small custom data format. Everything is also written to wheel_probe.log.
//
// Usage: wheel_probe.exe [seconds] [--ffb]
//   seconds  how long to read c_dfDIJoystick2 (default 15)
//   --ffb    also play a short, light constant force

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800

#include <windows.h>
#include <dinput.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>

static FILE* g_log = NULL;

static void out(const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);

    if (g_log)
    {
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        va_end(args);
        fprintf(g_log, "\r\n");
        fflush(g_log);
    }
}

static void narrow(const WCHAR* in, char* outText, int size)
{
    WideCharToMultiByte(CP_UTF8, 0, in, -1, outText, size, NULL, NULL);
    outText[size - 1] = '\0';
}

static void guid_text(REFGUID g, char* text, size_t size)
{
    snprintf(
        text,
        size,
        "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        (unsigned long)g.Data1, g.Data2, g.Data3,
        g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
        g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]
    );
}

struct EnumResult
{
    bool found;
    DIDEVICEINSTANCEW instance;
    int count;
};

static BOOL CALLBACK enum_devices(const DIDEVICEINSTANCEW* inst, VOID* ref)
{
    EnumResult* result = (EnumResult*)ref;
    char name[260];
    char product[260];
    char guidInstance[64];
    char guidProduct[64];

    narrow(inst->tszInstanceName, name, sizeof(name));
    narrow(inst->tszProductName, product, sizeof(product));
    guid_text(inst->guidInstance, guidInstance, sizeof(guidInstance));
    guid_text(inst->guidProduct, guidProduct, sizeof(guidProduct));

    out("  device '%s' product='%s' type=0x%08lx instance=%s product=%s",
        name, product, (unsigned long)inst->dwDevType, guidInstance, guidProduct);

    result->count++;

    if (!result->found && strstr(product, "G29"))
    {
        result->found = true;
        result->instance = *inst;
    }

    return DIENUM_CONTINUE;
}

static BOOL CALLBACK enum_objects(const DIDEVICEOBJECTINSTANCEW* obj, VOID*)
{
    char name[260];
    narrow(obj->tszName, name, sizeof(name));

    out("  object '%s' ofs=0x%02lx type=0x%08lx flags=0x%08lx usage=%u/0x%02x",
        name, (unsigned long)obj->dwOfs, (unsigned long)obj->dwType,
        (unsigned long)obj->dwFlags, obj->wUsagePage, obj->wUsage);

    return DIENUM_CONTINUE;
}

static void print_state(const DIJOYSTATE2& s, const char* prefix)
{
    char buttons[64] = {0};
    int n = 0;

    for (int i = 0; i < 32 && n < 60; ++i)
    {
        buttons[n++] = (s.rgbButtons[i] & 0x80) ? '1' : '.';
    }

    out("%s X=%6ld Y=%6ld Z=%6ld Rz=%6ld POV=%ld,%ld buttons=%s",
        prefix, (long)s.lX, (long)s.lY, (long)s.lZ, (long)s.lRz,
        (long)(LONG)s.rgdwPOV[0], (long)(LONG)s.rgdwPOV[1], buttons);
}

// A custom format: steering, the Z pedal, button 0 and the hat, packed.
struct CustomState
{
    LONG steering;
    LONG pedal;
    DWORD hat;
    BYTE button;
    BYTE padding[3];
};

int main(int argc, char** argv)
{
    int seconds = 15;
    bool ffb = false;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--ffb") == 0)
        {
            ffb = true;
        }
        else
        {
            seconds = atoi(argv[i]);
        }
    }

    g_log = fopen("wheel_probe.log", "wb");

    IDirectInput8W* di = NULL;
    HRESULT hr = DirectInput8Create(GetModuleHandleW(NULL), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, NULL);
    out("DirectInput8Create hr=0x%08lx", (unsigned long)hr);

    if (FAILED(hr))
    {
        return 1;
    }

    EnumResult result;
    ZeroMemory(&result, sizeof(result));

    out("EnumDevices GAMECTRL ATTACHEDONLY:");
    hr = di->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_devices, &result, DIEDFL_ATTACHEDONLY);
    out("  hr=0x%08lx count=%d", (unsigned long)hr, result.count);

    EnumResult ffResult;
    ZeroMemory(&ffResult, sizeof(ffResult));

    out("EnumDevices GAMECTRL FORCEFEEDBACK:");
    hr = di->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_devices, &ffResult, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    out("  hr=0x%08lx count=%d", (unsigned long)hr, ffResult.count);

    if (!result.found)
    {
        out("FAIL: no G29 enumerated");
        di->Release();
        return 2;
    }

    IDirectInputDevice8W* dev = NULL;
    hr = di->CreateDevice(result.instance.guidInstance, &dev, NULL);
    out("CreateDevice hr=0x%08lx", (unsigned long)hr);

    if (FAILED(hr))
    {
        di->Release();
        return 3;
    }

    DIDEVCAPS caps;
    ZeroMemory(&caps, sizeof(caps));
    caps.dwSize = sizeof(caps);
    hr = dev->GetCapabilities(&caps);
    out("GetCapabilities hr=0x%08lx flags=0x%08lx type=0x%08lx axes=%lu buttons=%lu povs=%lu FF=%s",
        (unsigned long)hr, (unsigned long)caps.dwFlags, (unsigned long)caps.dwDevType,
        (unsigned long)caps.dwAxes, (unsigned long)caps.dwButtons, (unsigned long)caps.dwPOVs,
        (caps.dwFlags & DIDC_FORCEFEEDBACK) ? "yes" : "no");

    DIPROPDWORD vidpid;
    ZeroMemory(&vidpid, sizeof(vidpid));
    vidpid.diph.dwSize = sizeof(vidpid);
    vidpid.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    vidpid.diph.dwHow = DIPH_DEVICE;
    hr = dev->GetProperty(DIPROP_VIDPID, &vidpid.diph);
    out("GetProperty VIDPID hr=0x%08lx value=0x%08lx", (unsigned long)hr, (unsigned long)vidpid.dwData);

    out("EnumObjects ALL:");
    dev->EnumObjects(enum_objects, NULL, DIDFT_ALL);

    hr = dev->SetDataFormat(&c_dfDIJoystick2);
    out("SetDataFormat c_dfDIJoystick2 hr=0x%08lx", (unsigned long)hr);

    hr = dev->SetCooperativeLevel(GetConsoleWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    out("SetCooperativeLevel hr=0x%08lx", (unsigned long)hr);

    DIPROPDWORD buffer;
    ZeroMemory(&buffer, sizeof(buffer));
    buffer.diph.dwSize = sizeof(buffer);
    buffer.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    buffer.diph.dwHow = DIPH_DEVICE;
    buffer.dwData = 64;
    hr = dev->SetProperty(DIPROP_BUFFERSIZE, &buffer.diph);
    out("SetProperty BUFFERSIZE 64 hr=0x%08lx", (unsigned long)hr);

    // Many games centre the steering axis on zero.
    DIPROPRANGE range;
    ZeroMemory(&range, sizeof(range));
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwObj = DIJOFS_X;
    range.diph.dwHow = DIPH_BYOFFSET;
    range.lMin = -32768;
    range.lMax = 32767;
    hr = dev->SetProperty(DIPROP_RANGE, &range.diph);
    out("SetProperty RANGE X -32768..32767 hr=0x%08lx", (unsigned long)hr);

    hr = dev->Acquire();
    out("Acquire hr=0x%08lx", (unsigned long)hr);

    out("Reading c_dfDIJoystick2 for %d s: turn the wheel, press each pedal and some buttons.", seconds);

    DIJOYSTATE2 last;
    memset(&last, 0xAB, sizeof(last));
    DWORD events = 0;
    DWORD reads = 0;
    DWORD end = GetTickCount() + (DWORD)seconds * 1000;

    while ((LONG)(end - GetTickCount()) > 0)
    {
        dev->Poll();

        DIJOYSTATE2 state;
        hr = dev->GetDeviceState(sizeof(state), &state);
        reads++;

        if (FAILED(hr))
        {
            out("GetDeviceState hr=0x%08lx", (unsigned long)hr);
            break;
        }

        if (memcmp(&state, &last, sizeof(state)) != 0)
        {
            print_state(state, "  state");
            last = state;
        }

        DIDEVICEOBJECTDATA data[64];
        DWORD count = 64;
        hr = dev->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), data, &count, 0);

        if (SUCCEEDED(hr))
        {
            events += count;
        }

        Sleep(50);
    }

    out("Buffered events received: %lu over %lu reads", (unsigned long)events, (unsigned long)reads);

    if (ffb)
    {
        DWORD axes[1] = {DIJOFS_X};
        LONG direction[1] = {0};
        DICONSTANTFORCE cf = {2500};
        DIEFFECT eff;
        ZeroMemory(&eff, sizeof(eff));
        eff.dwSize = sizeof(eff);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.dwDuration = INFINITE;
        eff.dwGain = DI_FFNOMINALMAX;
        eff.dwTriggerButton = DIEB_NOTRIGGER;
        eff.cAxes = 1;
        eff.rgdwAxes = axes;
        eff.rglDirection = direction;
        eff.cbTypeSpecificParams = sizeof(cf);
        eff.lpvTypeSpecificParams = &cf;

        IDirectInputEffect* effect = NULL;
        hr = dev->CreateEffect(GUID_ConstantForce, &eff, &effect, NULL);
        out("CreateEffect ConstantForce hr=0x%08lx", (unsigned long)hr);

        if (SUCCEEDED(hr) && effect)
        {
            effect->Start(1, 0);
            out("Light constant force for 1 s...");
            Sleep(1000);
            effect->Stop();
            effect->Release();
        }
    }

    dev->Unacquire();

    DIOBJECTDATAFORMAT objects[4] = {
        {&GUID_XAxis, FIELD_OFFSET(CustomState, steering), DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&GUID_ZAxis, FIELD_OFFSET(CustomState, pedal), DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&GUID_POV, FIELD_OFFSET(CustomState, hat), DIDFT_POV | DIDFT_ANYINSTANCE, 0},
        {NULL, FIELD_OFFSET(CustomState, button), DIDFT_BUTTON | DIDFT_MAKEINSTANCE(0), 0},
    };
    DIDATAFORMAT format = {
        sizeof(DIDATAFORMAT),
        sizeof(DIOBJECTDATAFORMAT),
        DIDF_ABSAXIS,
        sizeof(CustomState),
        4,
        objects
    };

    hr = dev->SetDataFormat(&format);
    out("SetDataFormat custom hr=0x%08lx", (unsigned long)hr);

    hr = dev->Acquire();
    out("Acquire hr=0x%08lx", (unsigned long)hr);

    CustomState custom;
    ZeroMemory(&custom, sizeof(custom));
    hr = dev->GetDeviceState(sizeof(custom), &custom);
    out("Custom state hr=0x%08lx steering=%ld pedalZ=%ld hat=%ld button0=0x%02x",
        (unsigned long)hr, (long)custom.steering, (long)custom.pedal, (long)(LONG)custom.hat, custom.button);

    dev->Unacquire();
    dev->Release();
    di->Release();

    out("DONE");

    if (g_log)
    {
        fclose(g_log);
    }

    return 0;
}
