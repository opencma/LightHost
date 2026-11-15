#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/keysym.h>
#elif defined(__APPLE__)
#include <Carbon/Carbon.h>
#endif

#include "GlobalBypassShortcut.hpp"
#include <JuceHeader.h>

#include <iterator>
#include <utility>

#if JUCE_WINDOWS
#include <Windows.h>
#elif JUCE_LINUX
#include <cstdlib>
#endif

#if JUCE_WINDOWS
struct GlobalBypassShortcut::Impl
{
    explicit Impl (std::function<void()> callbackIn) : callback (std::move (callbackIn))
    {
        const auto instance = GetModuleHandleW (nullptr);
        WNDCLASSW windowClass {};
        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = L"LightHostGlobalBypassShortcut";
        if (RegisterClassW (&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        window = CreateWindowExW (0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, instance, this);
    }

    ~Impl()
    {
        if (window != nullptr)
        {
            if (registeredId != 0)
                UnregisterHotKey (window, registeredId);
            DestroyWindow (window);
        }
    }

    String setShortcut (const String& description)
    {
        if (description.trim().isEmpty())
        {
            if (registeredId != 0)
                UnregisterHotKey (window, registeredId);
            registeredId = 0;
            registeredModifiers = registeredKey = 0;
            return {};
        }
        if (window == nullptr)
            return "Windows could not create the global shortcut listener.";

        StringArray parts;
        parts.addTokens (description, "+", "");
        parts.trim();
        if (parts.size() < 2)
            return "Use a modifier and a letter, number or function key, for example ctrl + b.";

        UINT modifiers = MOD_NOREPEAT;
        for (int i = 0; i < parts.size() - 1; ++i)
        {
            UINT flag = 0;
            if (parts[i].equalsIgnoreCase ("ctrl")) flag = MOD_CONTROL;
            else if (parts[i].equalsIgnoreCase ("alt")) flag = MOD_ALT;
            else if (parts[i].equalsIgnoreCase ("shift")) flag = MOD_SHIFT;
            else if (parts[i].equalsIgnoreCase ("win")) flag = MOD_WIN;
            if (flag == 0 || (modifiers & flag) != 0)
                return "Use ctrl, alt, shift or win once each before the key.";
            modifiers |= flag;
        }

        const auto text = parts[parts.size() - 1].toUpperCase();
        UINT key = 0;
        if (text.length() == 1 && ((text[0] >= 'A' && text[0] <= 'Z')
                                   || (text[0] >= '0' && text[0] <= '9')))
            key = (UINT) text[0];
        else if (text.startsWithChar ('F'))
            for (int i = 1; i <= 24; ++i)
                if (text == "F" + String (i))
                    key = VK_F1 + (UINT) i - 1;
        if (key == 0)
            return "The key must be a letter, number or F1 through F24.";
        if (registeredId != 0 && modifiers == registeredModifiers && key == registeredKey)
            return {};

        const int nextId = registeredId == 1 ? 2 : 1;
        if (! RegisterHotKey (window, nextId, modifiers, key))
            return "Windows could not register this global shortcut. It may already be in use.";
        if (registeredId != 0)
            UnregisterHotKey (window, registeredId);
        registeredId = nextId;
        registeredModifiers = modifiers;
        registeredKey = key;
        return {};
    }

    static LRESULT CALLBACK windowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*> (lParam);
            SetWindowLongPtrW (hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (create->lpCreateParams));
        }
        auto* self = reinterpret_cast<Impl*> (GetWindowLongPtrW (hwnd, GWLP_USERDATA));
        if (self != nullptr && message == WM_HOTKEY && (int) wParam == self->registeredId)
        {
            self->callback();
            return 0;
        }
        return DefWindowProcW (hwnd, message, wParam, lParam);
    }

    std::function<void()> callback;
    HWND window = nullptr;
    int registeredId = 0;
    UINT registeredModifiers = 0, registeredKey = 0;
};
#elif JUCE_MAC
struct GlobalBypassShortcut::Impl
{
    explicit Impl (std::function<void()> callbackIn) : callback (std::move (callbackIn))
    {
        EventTypeSpec eventType { kEventClassKeyboard, kEventHotKeyPressed };
        if (InstallEventHandler (GetApplicationEventTarget(), hotKeyPressed, 1, &eventType,
                                 this, &eventHandler) != noErr)
            eventHandler = nullptr;
    }

    ~Impl()
    {
        if (hotKey != nullptr)
            UnregisterEventHotKey (hotKey);
        if (eventHandler != nullptr)
            RemoveEventHandler (eventHandler);
    }

    String setShortcut (const String& description)
    {
        if (description.trim().isEmpty())
        {
            if (hotKey != nullptr)
                UnregisterEventHotKey (hotKey);
            hotKey = nullptr;
            return {};
        }
        if (eventHandler == nullptr)
            return "macOS could not create the global shortcut listener.";

        StringArray parts;
        parts.addTokens (description, "+", "");
        parts.trim();
        if (parts.size() < 2)
            return "Use a modifier and a letter, number or function key, for example ctrl + b.";

        UInt32 modifiers = 0;
        for (int i = 0; i < parts.size() - 1; ++i)
        {
            UInt32 flag = 0;
            if (parts[i].equalsIgnoreCase ("ctrl")) flag = controlKey;
            else if (parts[i].equalsIgnoreCase ("alt")) flag = optionKey;
            else if (parts[i].equalsIgnoreCase ("shift")) flag = shiftKey;
            else if (parts[i].equalsIgnoreCase ("cmd") || parts[i].equalsIgnoreCase ("command"))
                flag = cmdKey;
            if (flag == 0 || (modifiers & flag) != 0)
                return "Use ctrl, alt, shift or cmd once each before the key.";
            modifiers |= flag;
        }

        const auto keyCode = keyCodeFor (parts[parts.size() - 1]);
        if (keyCode < 0)
            return "The key must be a letter, number or F1 through F20.";

        const EventHotKeyID identifier { 'LHst', 1 };
        EventHotKeyRef nextHotKey = nullptr;
        if (RegisterEventHotKey ((UInt32) keyCode, modifiers, identifier, GetApplicationEventTarget(),
                                 0, &nextHotKey) != noErr)
            return "macOS could not register this global shortcut. It may already be in use.";
        if (hotKey != nullptr)
            UnregisterEventHotKey (hotKey);
        hotKey = nextHotKey;
        return {};
    }

    static int keyCodeFor (String text)
    {
        text = text.toLowerCase();
        static const std::pair<const char*, int> keyCodes[] =
        {
            { "a", kVK_ANSI_A }, { "b", kVK_ANSI_B }, { "c", kVK_ANSI_C }, { "d", kVK_ANSI_D },
            { "e", kVK_ANSI_E }, { "f", kVK_ANSI_F }, { "g", kVK_ANSI_G }, { "h", kVK_ANSI_H },
            { "i", kVK_ANSI_I }, { "j", kVK_ANSI_J }, { "k", kVK_ANSI_K }, { "l", kVK_ANSI_L },
            { "m", kVK_ANSI_M }, { "n", kVK_ANSI_N }, { "o", kVK_ANSI_O }, { "p", kVK_ANSI_P },
            { "q", kVK_ANSI_Q }, { "r", kVK_ANSI_R }, { "s", kVK_ANSI_S }, { "t", kVK_ANSI_T },
            { "u", kVK_ANSI_U }, { "v", kVK_ANSI_V }, { "w", kVK_ANSI_W }, { "x", kVK_ANSI_X },
            { "y", kVK_ANSI_Y }, { "z", kVK_ANSI_Z }, { "0", kVK_ANSI_0 }, { "1", kVK_ANSI_1 },
            { "2", kVK_ANSI_2 }, { "3", kVK_ANSI_3 }, { "4", kVK_ANSI_4 }, { "5", kVK_ANSI_5 },
            { "6", kVK_ANSI_6 }, { "7", kVK_ANSI_7 }, { "8", kVK_ANSI_8 }, { "9", kVK_ANSI_9 }
        };
        for (const auto& pair : keyCodes)
            if (text == pair.first)
                return pair.second;
        static const UInt32 functionKeys[] =
        {
            kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8, kVK_F9, kVK_F10,
            kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20
        };
        if (text.startsWithChar ('f'))
        {
            const int number = text.substring (1).getIntValue();
            if (number >= 1 && number <= (int) std::size (functionKeys) && text == "f" + String (number))
                return (int) functionKeys[number - 1];
        }
        return -1;
    }

    static OSStatus hotKeyPressed (EventHandlerCallRef, EventRef event, void* userData)
    {
        EventHotKeyID identifier {};
        if (GetEventParameter (event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                               sizeof (identifier), nullptr, &identifier) == noErr)
            static_cast<Impl*> (userData)->callback();
        return noErr;
    }

    std::function<void()> callback;
    EventHandlerRef eventHandler = nullptr;
    EventHotKeyRef hotKey = nullptr;
};
#elif JUCE_LINUX
namespace
{
Display* activeErrorDisplay = nullptr;
bool hotKeyGrabFailed = false;
XErrorHandler previousXErrorHandler = nullptr;

int hotKeyXErrorHandler (Display* display, XErrorEvent* event)
{
    if (display == activeErrorDisplay && event->error_code == BadAccess)
        hotKeyGrabFailed = true;
    else if (previousXErrorHandler != nullptr)
        return previousXErrorHandler (display, event);
    return 0;
}

bool parseX11Shortcut (const String& description, KeySym& symbol, unsigned int& modifiers, String& error)
{
    StringArray parts;
    parts.addTokens (description, "+", "");
    parts.trim();
    if (parts.size() < 2)
    {
        error = "Use a modifier and a letter, number or function key, for example ctrl + b.";
        return false;
    }

    modifiers = 0;
    for (int i = 0; i < parts.size() - 1; ++i)
    {
        unsigned int flag = 0;
        if (parts[i].equalsIgnoreCase ("ctrl")) flag = ControlMask;
        else if (parts[i].equalsIgnoreCase ("alt")) flag = Mod1Mask;
        else if (parts[i].equalsIgnoreCase ("shift")) flag = ShiftMask;
        else if (parts[i].equalsIgnoreCase ("win") || parts[i].equalsIgnoreCase ("super"))
            flag = Mod4Mask;
        if (flag == 0 || (modifiers & flag) != 0)
        {
            error = "Use ctrl, alt, shift or super once each before the key.";
            return false;
        }
        modifiers |= flag;
    }

    auto keyName = parts[parts.size() - 1].toLowerCase();
    symbol = NoSymbol;
    if (keyName.length() == 1)
        symbol = XStringToKeysym (keyName.toRawUTF8());
    else if (keyName.startsWithChar ('f'))
    {
        const int number = keyName.substring (1).getIntValue();
        if (number >= 1 && number <= 24 && keyName == "f" + String (number))
            symbol = static_cast<KeySym> (XK_F1 + number - 1);
    }
    if (symbol == NoSymbol)
    {
        error = "The key must be a letter, number or F1 through F24.";
        return false;
    }
    return true;
}
}

struct GlobalBypassShortcut::Impl
{
    explicit Impl (std::function<void()> callbackIn) : callback (std::move (callbackIn))
    {
        if (std::getenv ("WAYLAND_DISPLAY") != nullptr)
            error = "Wayland does not provide a portable global shortcut API to this X11 app.";
        else
            display = XOpenDisplay (nullptr);
        if (display == nullptr && error.isEmpty())
            error = "Linux could not connect to the X11 display for a global shortcut.";
        if (display != nullptr)
            root = DefaultRootWindow (display);
    }

    ~Impl()
    {
        if (display != nullptr)
        {
            unregisterKey (registeredKey, registeredModifiers);
            XCloseDisplay (display);
        }
    }

    String setShortcut (const String& description)
    {
        if (description.trim().isEmpty())
        {
            unregisterKey (registeredKey, registeredModifiers);
            registeredKey = 0;
            registeredModifiers = 0;
            return {};
        }
        if (error.isNotEmpty())
            return error;

        KeySym symbol = NoSymbol;
        unsigned int nextModifiers = 0;
        String parseError;
        if (! parseX11Shortcut (description, symbol, nextModifiers, parseError))
            return parseError;
        const auto nextKey = XKeysymToKeycode (display, symbol);
        if (nextKey == 0)
            return "X11 could not map the requested key.";
        if (nextKey == registeredKey && nextModifiers == registeredModifiers)
            return {};

        bool failed = false;
        activeErrorDisplay = display;
        hotKeyGrabFailed = false;
        previousXErrorHandler = XSetErrorHandler (hotKeyXErrorHandler);
        grabKey (nextKey, nextModifiers);
        XSync (display, False);
        failed = hotKeyGrabFailed;
        XSetErrorHandler (previousXErrorHandler);
        activeErrorDisplay = nullptr;
        previousXErrorHandler = nullptr;
        if (failed)
        {
            unregisterKey (nextKey, nextModifiers);
            return "X11 could not register this global shortcut. It may already be in use.";
        }

        unregisterKey (registeredKey, registeredModifiers);
        registeredKey = nextKey;
        registeredModifiers = nextModifiers;
        return {};
    }

    void poll()
    {
        if (display == nullptr)
            return;
        while (XPending (display) > 0)
        {
            XEvent event {};
            XNextEvent (display, &event);
            if (event.type == 2 && event.xkey.keycode == registeredKey
                && (event.xkey.state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask))
                       == registeredModifiers)
                callback();
        }
    }

    void grabKey (KeyCode key, unsigned int modifiers)
    {
        const unsigned int lockMasks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
        for (const auto lockMask : lockMasks)
            XGrabKey (display, key, modifiers | lockMask, root, False, GrabModeAsync, GrabModeAsync);
        XFlush (display);
    }

    void unregisterKey (KeyCode key, unsigned int modifiers)
    {
        if (display == nullptr || key == 0)
            return;
        const unsigned int lockMasks[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
        for (const auto lockMask : lockMasks)
            XUngrabKey (display, key, modifiers | lockMask, root);
        XFlush (display);
    }

    std::function<void()> callback;
    Display* display = nullptr;
    Window root = 0;
    KeyCode registeredKey = 0;
    unsigned int registeredModifiers = 0;
    String error;
};
#endif

GlobalBypassShortcut::GlobalBypassShortcut (std::function<void()> callback)
    : impl (std::make_unique<Impl> (std::move (callback)))
{
}

GlobalBypassShortcut::~GlobalBypassShortcut() = default;

juce::String GlobalBypassShortcut::setShortcut (const juce::String& description)
{
    return impl->setShortcut (description);
}

void GlobalBypassShortcut::poll()
{
#if JUCE_LINUX
    impl->poll();
#endif
}
