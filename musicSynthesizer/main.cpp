#include <ntddk.h>
#define IOCTL_START_MUSIC CTL_CODE(FILE_DEVICE_KEYBOARD, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define KEY_MAKE  0
#define KEY_BREAK 1

PDEVICE_OBJECT myKbdDevice = NULL;
ULONG pendingkey = 0;

struct KeyData {
    char key;
    bool isPressed;
};

typedef struct {
    PDEVICE_OBJECT LowerKbdDevice;
    PIRP PendingIrp;  // Додано для зберігання очікуючого IRP
} DEVICE_EXTENSION, * PDEVICE_EXTENSION;

typedef struct _KEYBOARD_INPUT_DATA {
    USHORT UnitId;
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    ULONG ExtraInformation;
} KEYBOARD_INPUT_DATA, * PKEYBOARD_INPUT_DATA;

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    LARGE_INTEGER interval = { 0 };
    PDEVICE_OBJECT DeviceObject = DriverObject->DeviceObject;
    UNICODE_STRING SymbolicLink = RTL_CONSTANT_STRING(L"\\DosDevices\\MyKeyboardDevice");
    interval.QuadPart = -10 * 1000 * 1000;

    IoDeleteSymbolicLink(&SymbolicLink);

    IoDetachDevice(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerKbdDevice);

    while (pendingkey > 0) {
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
    }

    IoDeleteDevice(myKbdDevice);
    KdPrint(("Unload Our Driver\r\n"));
}

NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_START_MUSIC:
    {
        PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

        // Зберігаємо IRP для подальшого використання
        deviceExtension->PendingIrp = Irp;

        // Позначаємо IRP як очікуючий
        IoMarkIrpPending(Irp);

        // Не завершуємо IRP тут - він буде завершений в ReadComplete
        return STATUS_PENDING;
    }
    break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Status = status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        break;
    }

    return status;
}

NTSTATUS DispatchPass(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    IoCopyCurrentIrpStackLocationToNext(Irp);
    return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerKbdDevice, Irp);
}

NTSTATUS ReadComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context) {
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);

    USHORT targetKeys[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16 }; // Скан-коди для q, w, e, r, t, y, u
    CHAR keymap[] = { 'q', 'w', 'e', 'r', 't', 'y', 'u' };
    PKEYBOARD_INPUT_DATA Keys = (PKEYBOARD_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
    SIZE_T structnum = Irp->IoStatus.Information / sizeof(KEYBOARD_INPUT_DATA);

    if (NT_SUCCESS(Irp->IoStatus.Status)) {
        for (SIZE_T i = 0; i < structnum; i++) {
            for (int j = 0; j < sizeof(targetKeys) / sizeof(targetKeys[0]); j++) {
                if (Keys[i].MakeCode == targetKeys[j]) {
                    // Створюємо структуру для передачі даних
                    KeyData keyData;
                    keyData.key = keymap[j];
                    // Якщо Flags дорівнює KEY_MAKE (0), клавіша натиснута
                    keyData.isPressed = (Keys[i].Flags == KEY_MAKE);

                    KdPrint(("Keyboard Event - Key: %c, Flags: %x, isPressed: %d\n",
                        keyData.key, Keys[i].Flags, keyData.isPressed));

                    // Зберігаємо дані для передачі користувацькому додатку
                    if (DeviceObject->DeviceExtension != NULL) {
                        PDEVICE_EXTENSION deviceExtension = (PDEVICE_EXTENSION)DeviceObject->DeviceExtension;

                        // Перевіряємо, чи є активний IRP для IOCTL
                        if (deviceExtension->PendingIrp != NULL) {
                            PIRP pendingIrp = deviceExtension->PendingIrp;
                            PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(pendingIrp);

                            // Копіюємо дані в буфер користувача
                            if (irpSp->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(KeyData)) {
                                RtlCopyMemory(pendingIrp->AssociatedIrp.SystemBuffer, &keyData, sizeof(KeyData));
                                pendingIrp->IoStatus.Information = sizeof(KeyData);
                                pendingIrp->IoStatus.Status = STATUS_SUCCESS;

                                // Завершуємо IRP
                                IoCompleteRequest(pendingIrp, IO_NO_INCREMENT);
                                deviceExtension->PendingIrp = NULL;

                                KdPrint(("Data sent to user application\n"));
                            }
                        }
                        else {
                            KdPrint(("No pending IRP available\n"));
                        }
                    }
                    break;
                }
            }
        }
    }

    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }

    pendingkey--;
    return Irp->IoStatus.Status;
}

NTSTATUS DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    KdPrint(("Driver: Create request received\n"));

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

NTSTATUS DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    KdPrint(("Driver: Close request received\n"));

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

NTSTATUS DispatchCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    KdPrint(("Driver: Cleanup request received\n"));

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


NTSTATUS DispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, ReadComplete, NULL, TRUE, TRUE, TRUE);
    pendingkey++;
    return IoCallDriver(((PDEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerKbdDevice, Irp);
}

NTSTATUS MyAttachDevice(PDRIVER_OBJECT DriverObject) {
    NTSTATUS status;
    UNICODE_STRING TargetDevice = RTL_CONSTANT_STRING(L"\\Device\\KeyboardClass0");
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\MyKeyboardDevice");
    UNICODE_STRING SymbolicLink = RTL_CONSTANT_STRING(L"\\DosDevices\\MyKeyboardDevice");

    status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENSION), &DeviceName, FILE_DEVICE_KEYBOARD, 0, FALSE, &myKbdDevice);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Failed to create device: %08x\n", status));
        return status;
    }

    myKbdDevice->Flags |= DO_BUFFERED_IO;
    myKbdDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    status = IoCreateSymbolicLink(&SymbolicLink, &DeviceName);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Failed to create symbolic link: %08x\n", status));
        IoDeleteDevice(myKbdDevice);
        return status;
    }

    RtlZeroMemory(myKbdDevice->DeviceExtension, sizeof(DEVICE_EXTENSION));

    status = IoAttachDevice(myKbdDevice, &TargetDevice, &((PDEVICE_EXTENSION)myKbdDevice->DeviceExtension)->LowerKbdDevice);
    if (!NT_SUCCESS(status)) {
        KdPrint(("Failed to attach device: %08x\n", status));
        IoDeleteSymbolicLink(&SymbolicLink);
        IoDeleteDevice(myKbdDevice);
        return status;
    }

    return STATUS_SUCCESS;
}


extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    UNREFERENCED_PARAMETER(RegistryPath);
    int i;
    DriverObject->DriverUnload = DriverUnload;


    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = DispatchPass;
    }


    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_READ] = DispatchRead;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

    KdPrint(("driver is loaded\r\n"));
    status = MyAttachDevice(DriverObject);
    if (!NT_SUCCESS(status)) {
        KdPrint(("ataching is failing\r\n"));
    }

    else {
        KdPrint(("ataching succeeds\r\n"));
    }
    return status;
}
