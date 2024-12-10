#include <windows.h>
#include <iostream>
#include <mmsystem.h>
#include <map>
#include <string>
#include <thread> 

#pragma comment(lib, "winmm.lib")

#define IOCTL_START_MUSIC CTL_CODE(FILE_DEVICE_KEYBOARD, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct KeyData {
    char key;
    bool isPressed;
};

std::map<char, std::wstring> keyToNote = {
    {'q', L"c1.wav"},  // до
    {'w', L"d1.wav"},  // ре
    {'e', L"e1.wav"},  // мі
    {'r', L"f1.wav"},  // фа
    {'t', L"g1.wav"},  // соль
    {'y', L"a1.wav"},  // ля
    {'u', L"b1.wav"}   // сі
};

void PlayNote(const std::wstring& noteFile) {
    PlaySound(noteFile.c_str(), NULL, SND_FILENAME | SND_ASYNC);
}

bool exitRequested = false; 

void CheckExit() {
    while (!exitRequested) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            std::cout << "Вихід з програми...\n";
            exitRequested = true; 
        }
        Sleep(10);
    }
}

int main() {
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
    HANDLE hDevice = CreateFile(
        L"\\\\.\\MyKeyboardDevice",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        std::cerr << "Не вдалося відкрити пристрій. Код помилки: " << GetLastError() << std::endl;
        return 1;
    }

    std::cout << "Пристрій відкрито успішно. Натисніть на відповідні клавіші: \n";
    std::cout << "Q - До\nW - Ре\nE - Mi\nR - Фa\n";
    std::cout << "T - Соль\nY - Ля\nU - Сі\n";
    std::cout << "Натисність Esc для виходу з програми.\n";

    std::thread exitThread(CheckExit);

    KeyData keyData;
    DWORD bytesReturned;

    while (!exitRequested) {
        BOOL result = DeviceIoControl(
            hDevice,
            IOCTL_START_MUSIC,
            NULL,
            0,
            &keyData,
            sizeof(KeyData),
            &bytesReturned,
            NULL
        );

        if (result && bytesReturned > 0) {
            if (keyData.isPressed) {
                auto it = keyToNote.find(keyData.key);
                if (it != keyToNote.end()) {
                    std::cout << "Грає нота: " << keyData.key << std::endl;
                    PlayNote(it->second);
                }
            }
        }

        Sleep(10);
    }

    exitThread.join();

    CloseHandle(hDevice);
    return 0;
}